// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/signaling/ftl_registration_manager.h"

#include <utility>

#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "remoting/signaling/ftl_device_id_provider.h"
#include "remoting/signaling/ftl_grpc_context.h"
#include "remoting/signaling/grpc_support/grpc_async_unary_request.h"
#include "remoting/signaling/grpc_support/grpc_authenticated_executor.h"
#include "remoting/signaling/grpc_support/grpc_executor.h"

namespace remoting {

namespace {

constexpr remoting::ftl::FtlCapability::Feature kFtlCapabilities[] = {
    remoting::ftl::FtlCapability_Feature_RECEIVE_CALLS_FROM_GAIA,
    remoting::ftl::FtlCapability_Feature_GAIA_REACHABLE};
constexpr size_t kFtlCapabilityCount =
    sizeof(kFtlCapabilities) / sizeof(ftl::FtlCapability::Feature);

constexpr base::TimeDelta kRefreshBufferTime = base::TimeDelta::FromHours(1);

}  // namespace

FtlRegistrationManager::FtlRegistrationManager(
    OAuthTokenGetter* token_getter,
    std::unique_ptr<FtlDeviceIdProvider> device_id_provider)
    : executor_(std::make_unique<GrpcAuthenticatedExecutor>(token_getter)),
      device_id_provider_(std::move(device_id_provider)) {
  DCHECK(device_id_provider_);
  registration_stub_ = Registration::NewStub(FtlGrpcContext::CreateChannel());
}

FtlRegistrationManager::~FtlRegistrationManager() = default;

void FtlRegistrationManager::SignInGaia(DoneCallback on_done) {
  ftl::SignInGaiaRequest request;
  *request.mutable_header() = FtlGrpcContext::CreateRequestHeader();
  request.set_app(FtlGrpcContext::GetChromotingAppIdentifier());
  request.set_mode(ftl::SignInGaiaMode_Value_DEFAULT_CREATE_ACCOUNT);

  request.mutable_register_data()->mutable_device_id()->set_id(
      device_id_provider_->GetDeviceId());

  // TODO(yuweih): Consider using different device ID type.
  request.mutable_register_data()->mutable_device_id()->set_type(
      ftl::DeviceIdType_Type_WEB_UUID);

  for (size_t i = 0; i < kFtlCapabilityCount; i++) {
    request.mutable_register_data()->add_caps(kFtlCapabilities[i]);
  }

  auto grpc_request = CreateGrpcAsyncUnaryRequest(
      base::BindOnce(&Registration::Stub::AsyncSignInGaia,
                     base::Unretained(registration_stub_.get())),
      FtlGrpcContext::CreateClientContext(), request,
      base::BindOnce(&FtlRegistrationManager::OnSignInGaiaResponse,
                     base::Unretained(this), std::move(on_done)));
  executor_->ExecuteRpc(std::move(grpc_request));
}

bool FtlRegistrationManager::IsSignedIn() const {
  return !ftl_auth_token_.empty();
}

std::string FtlRegistrationManager::GetRegistrationId() const {
  return registration_id_;
}
std::string FtlRegistrationManager::GetFtlAuthToken() const {
  return ftl_auth_token_;
}

void FtlRegistrationManager::OnSignInGaiaResponse(
    DoneCallback on_done,
    const grpc::Status& status,
    const ftl::SignInGaiaResponse& response) {
  registration_id_.clear();

  if (!status.ok()) {
    LOG(ERROR) << "Failed to sign in."
               << " Error code: " << status.error_code()
               << ", message: " << status.error_message();
    std::move(on_done).Run(status);
    return;
  }

  registration_id_ = response.registration_id();
  if (registration_id_.empty()) {
    std::move(on_done).Run(
        grpc::Status(grpc::StatusCode::UNKNOWN, "registration_id is empty."));
    return;
  }

  // TODO(yuweih): Consider caching auth token.
  ftl_auth_token_ = response.auth_token().payload();
  VLOG(0) << "Auth token set on FtlClient";
  base::TimeDelta refresh_delay =
      base::TimeDelta::FromMicroseconds(response.auth_token().expires_in());
  if (refresh_delay > kRefreshBufferTime) {
    refresh_delay -= kRefreshBufferTime;
  } else {
    LOG(WARNING) << "Refresh time is too short. Buffer time is not applied.";
  }
  sign_in_refresh_timer_.Start(
      FROM_HERE, refresh_delay,
      base::BindOnce(&FtlRegistrationManager::SignInGaia,
                     base::Unretained(this),
                     base::DoNothing::Once<const grpc::Status&>()));
  VLOG(0) << "Scheduled auth token refresh in: " << refresh_delay;
  std::move(on_done).Run(status);
}

}  // namespace remoting
