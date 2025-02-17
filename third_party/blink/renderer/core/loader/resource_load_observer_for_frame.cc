// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/resource_load_observer_for_frame.h"

#include "third_party/blink/renderer/core/core_probes_inl.h"
#include "third_party/blink/renderer/core/frame/frame_console.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_client.h"
#include "third_party/blink/renderer/core/frame/use_counter.h"
#include "third_party/blink/renderer/core/frame/web_feature.h"
#include "third_party/blink/renderer/core/loader/document_loader.h"
#include "third_party/blink/renderer/core/loader/frame_loader.h"
#include "third_party/blink/renderer/core/loader/frame_or_imported_document.h"
#include "third_party/blink/renderer/core/loader/idleness_detector.h"
#include "third_party/blink/renderer/core/loader/interactive_detector.h"
#include "third_party/blink/renderer/core/loader/mixed_content_checker.h"
#include "third_party/blink/renderer/core/loader/preload_helper.h"
#include "third_party/blink/renderer/core/loader/progress_tracker.h"
#include "third_party/blink/renderer/core/loader/subresource_filter.h"
#include "third_party/blink/renderer/platform/loader/fetch/fetch_client_settings_object.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_fetcher_properties.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_request.h"
#include "third_party/blink/renderer/platform/loader/fetch/resource_response.h"

namespace blink {

ResourceLoadObserverForFrame::ResourceLoadObserverForFrame(
    const FrameOrImportedDocument& frame_or_imported_document,
    const ResourceFetcherProperties& fetcher_properties)
    : frame_or_imported_document_(frame_or_imported_document),
      fetcher_properties_(fetcher_properties) {}
ResourceLoadObserverForFrame::~ResourceLoadObserverForFrame() = default;

void ResourceLoadObserverForFrame::WillSendRequest(
    uint64_t identifier,
    const ResourceRequest& request,
    const ResourceResponse& redirect_response,
    ResourceType resource_type,
    const FetchInitiatorInfo& initiator_info) {
  LocalFrame& frame = frame_or_imported_document_->GetFrame();
  if (redirect_response.IsNull()) {
    // Progress doesn't care about redirects, only notify it when an
    // initial request is sent.
    frame.Loader().Progress().WillStartLoading(identifier, request.Priority());
  }
  DocumentLoader& document_loader =
      frame_or_imported_document_->GetMasterDocumentLoader();
  Document& document = frame_or_imported_document_->GetDocument();
  probe::WillSendRequest(
      GetProbe(), identifier, &document_loader,
      fetcher_properties_->GetFetchClientSettingsObject().GlobalObjectUrl(),
      request, redirect_response, initiator_info, resource_type);
  if (auto* idleness_detector = frame.GetIdlenessDetector())
    idleness_detector->OnWillSendRequest(document.Fetcher());
  if (auto* interactive_detector = InteractiveDetector::From(document))
    interactive_detector->OnResourceLoadBegin(base::nullopt);
}

void ResourceLoadObserverForFrame::DidReceiveResponse(
    uint64_t identifier,
    const ResourceRequest& request,
    const ResourceResponse& response,
    Resource* resource,
    ResponseSource response_source) {
  LocalFrame& frame = frame_or_imported_document_->GetFrame();
  DocumentLoader& document_loader =
      frame_or_imported_document_->GetMasterDocumentLoader();
  LocalFrameClient* frame_client = frame.Client();
  SubresourceFilter* subresource_filter =
      document_loader.GetSubresourceFilter();
  if (subresource_filter && resource->GetResourceRequest().IsAdResource())
    subresource_filter->ReportAdRequestId(response.RequestId());

  DCHECK(frame_client);
  if (response.GetCTPolicyCompliance() ==
      ResourceResponse::kCTPolicyDoesNotComply) {
    CountUsage(
        frame.IsMainFrame()
            ? WebFeature::
                  kCertificateTransparencyNonCompliantSubresourceInMainFrame
            : WebFeature::
                  kCertificateTransparencyNonCompliantResourceInSubframe);
  }

  if (response_source == ResponseSource::kFromMemoryCache) {
    frame_client->DispatchDidLoadResourceFromMemoryCache(
        resource->GetResourceRequest(), response);

    // Note: probe::WillSendRequest needs to precede before this probe method.
    probe::MarkResourceAsCached(&frame, &document_loader, identifier);
    if (response.IsNull())
      return;
  }

  MixedContentChecker::CheckMixedPrivatePublic(&frame,
                                               response.RemoteIPAddress());

  PreloadHelper::CanLoadResources resource_loading_policy =
      response_source == ResponseSource::kFromMemoryCache
          ? PreloadHelper::kDoNotLoadResources
          : PreloadHelper::kLoadResourcesAndPreconnect;
  PreloadHelper::LoadLinksFromHeader(
      response.HttpHeaderField(http_names::kLink), response.CurrentRequestUrl(),
      frame, &frame_or_imported_document_->GetDocument(),
      resource_loading_policy, PreloadHelper::kLoadAll, nullptr);

  if (response.HasMajorCertificateErrors()) {
    MixedContentChecker::HandleCertificateError(&frame, response,
                                                request.GetRequestContext());
  }

  if (response.IsLegacyTLSVersion()) {
    CountUsage(WebFeature::kLegacyTLSVersionInSubresource);
    frame_client->ReportLegacyTLSVersion(response.CurrentRequestUrl());
  }

  frame.Loader().Progress().IncrementProgress(identifier, response);
  frame_client->DispatchDidReceiveResponse(response);
  probe::DidReceiveResourceResponse(GetProbe(), identifier, &document_loader,
                                    response, resource);
  // It is essential that inspector gets resource response BEFORE console.
  frame.Console().ReportResourceResponseReceived(&document_loader, identifier,
                                                 response);
}

void ResourceLoadObserverForFrame::DidReceiveData(
    uint64_t identifier,
    base::span<const char> chunk) {
  LocalFrame& frame = frame_or_imported_document_->GetFrame();
  DocumentLoader& document_loader =
      frame_or_imported_document_->GetMasterDocumentLoader();
  frame.Loader().Progress().IncrementProgress(identifier, chunk.size());
  probe::DidReceiveData(GetProbe(), identifier, &document_loader, chunk.data(),
                        chunk.size());
}

void ResourceLoadObserverForFrame::DidReceiveTransferSizeUpdate(
    uint64_t identifier,
    int transfer_size_diff) {
  DCHECK_GT(transfer_size_diff, 0);
  DocumentLoader& document_loader =
      frame_or_imported_document_->GetMasterDocumentLoader();
  probe::DidReceiveEncodedDataLength(GetProbe(), &document_loader, identifier,
                                     transfer_size_diff);
}

void ResourceLoadObserverForFrame::DidDownloadToBlob(uint64_t identifier,
                                                     BlobDataHandle* blob) {
  if (blob) {
    probe::DidReceiveBlob(
        GetProbe(), identifier,
        &frame_or_imported_document_->GetMasterDocumentLoader(), blob);
  }
}

void ResourceLoadObserverForFrame::DidFinishLoading(
    uint64_t identifier,
    TimeTicks finish_time,
    int64_t encoded_data_length,
    int64_t decoded_body_length,
    bool should_report_corb_blocking,
    ResponseSource response_source) {
  LocalFrame& frame = frame_or_imported_document_->GetFrame();
  DocumentLoader& document_loader =
      frame_or_imported_document_->GetMasterDocumentLoader();
  frame.Loader().Progress().CompleteProgress(identifier);
  probe::DidFinishLoading(GetProbe(), identifier, &document_loader, finish_time,
                          encoded_data_length, decoded_body_length,
                          should_report_corb_blocking);

  Document& document = frame_or_imported_document_->GetDocument();
  if (auto* interactive_detector = InteractiveDetector::From(document)) {
    interactive_detector->OnResourceLoadEnd(finish_time);
  }
  if (LocalFrame* frame = document.GetFrame()) {
    if (IdlenessDetector* idleness_detector = frame->GetIdlenessDetector()) {
      idleness_detector->OnDidLoadResource();
    }
  }
  if (response_source == ResponseSource::kNotFromMemoryCache) {
    document.CheckCompleted();
  }
}

void ResourceLoadObserverForFrame::DidFailLoading(const KURL&,
                                                  uint64_t identifier,
                                                  const ResourceError& error,
                                                  int64_t,
                                                  bool is_internal_request) {
  LocalFrame& frame = frame_or_imported_document_->GetFrame();
  DocumentLoader& document_loader =
      frame_or_imported_document_->GetMasterDocumentLoader();
  frame.Loader().Progress().CompleteProgress(identifier);
  probe::DidFailLoading(GetProbe(), identifier, &document_loader, error);

  // Notification to FrameConsole should come AFTER InspectorInstrumentation
  // call, DevTools front-end relies on this.
  if (!is_internal_request) {
    frame.Console().DidFailLoading(&document_loader, identifier, error);
  }
  Document& document = frame_or_imported_document_->GetDocument();
  if (auto* interactive_detector = InteractiveDetector::From(document)) {
    // We have not yet recorded load_finish_time. Pass nullopt here; we will
    // call CurrentTimeTicksInSeconds lazily when we need it.
    interactive_detector->OnResourceLoadEnd(base::nullopt);
  }
  if (LocalFrame* frame = document.GetFrame()) {
    if (IdlenessDetector* idleness_detector = frame->GetIdlenessDetector()) {
      idleness_detector->OnDidLoadResource();
    }
  }
  document.CheckCompleted();
}

void ResourceLoadObserverForFrame::Trace(Visitor* visitor) {
  visitor->Trace(frame_or_imported_document_);
  visitor->Trace(fetcher_properties_);
  ResourceLoadObserver::Trace(visitor);
}

CoreProbeSink* ResourceLoadObserverForFrame::GetProbe() {
  return probe::ToCoreProbeSink(
      frame_or_imported_document_->GetFrame().GetDocument());
}

void ResourceLoadObserverForFrame::CountUsage(WebFeature feature) {
  frame_or_imported_document_->GetMasterDocumentLoader().GetUseCounter().Count(
      feature, &frame_or_imported_document_->GetFrame());
}

}  // namespace blink
