// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// <include src="post_message_channel.js">
// <include src="webview_event_manager.js">

/**
 * @fileoverview Saml support for webview based auth.
 */

cr.define('cr.login', function() {
  'use strict';

  /**
   * The lowest version of the credentials passing API supported.
   * @type {number}
   */
  const MIN_API_VERSION_VERSION = 1;

  /**
   * The highest version of the credentials passing API supported.
   * @type {number}
   */
  const MAX_API_VERSION_VERSION = 1;

  /**
   * The key types supported by the credentials passing API.
   * @type {Array} Array of strings.
   */
  const API_KEY_TYPES = [
    'KEY_TYPE_PASSWORD_PLAIN',
  ];

  /** @const */
  const SAML_HEADER = 'google-accounts-saml';

  /** @const */
  const injectedScriptName = 'samlInjected';

  /**
   * The script to inject into webview and its sub frames.
   * @type {string}
   */
  const injectedJs = String.raw`
      // <include src="webview_saml_injected.js">
  `;

  /**
   * Creates a new URL by striping all query parameters.
   * @param {string} url The original URL.
   * @return {string} The new URL with all query parameters stripped.
   */
  function stripParams(url) {
    return url.substring(0, url.indexOf('?')) || url;
  }

  /**
   * Extract domain name from an URL.
   * @param {string} url An URL string.
   * @return {string} The host name of the URL.
   */
  function extractDomain(url) {
    const a = document.createElement('a');
    a.href = url;
    return a.hostname;
  }

  /**
   * A handler to provide saml support for the given webview that hosts the
   * auth IdP pages.
   */
  class SamlHandler extends cr.EventTarget {
    /**
     * @param {webview} webview
     * @param {boolean} startsOnSamlPage - whether initial URL is already SAML
     *                  page
     * */
    constructor(webview, startsOnSamlPage) {
      super();

      /**
       * The webview that serves IdP pages.
       * @type {webview}
       */
      this.webview_ = webview;

      /**
       * Whether a Saml page is in the webview from the start.
       */
      this.startsOnSamlPage_ = startsOnSamlPage;

      /**
       * Whether a Saml IdP page is display in the webview.
       * @type {boolean}
       */
      this.isSamlPage_ = this.startsOnSamlPage_;

      /**
       * Pending Saml IdP page flag that is set when a SAML_HEADER is received
       * and is copied to |isSamlPage_| in loadcommit.
       * @type {boolean}
       */
      this.pendingIsSamlPage_ = this.startsOnSamlPage_;

      /**
       * The last aborted top level url. It is recorded in loadabort event and
       * used to skip injection into Chrome's error page in the following
       * loadcommit event.
       * @type {string}
       */
      this.abortedTopLevelUrl_ = null;

      /**
       * The domain of the Saml IdP.
       * @type {string}
       */
      this.authDomain = '';

      /**
       * Scraped password stored in an id to password field value map.
       * @type {Object<string, string>}
       * @private
       */
      this.passwordStore_ = {};

      /**
       * Whether Saml API is initialized.
       * @type {boolean}
       */
      this.apiInitialized_ = false;

      /**
       * Saml API version to use.
       * @type {number}
       */
      this.apiVersion_ = 0;

      /**
       * Saml API token received.
       * @type {string}
       */
      this.apiToken_ = null;

      /**
       * Saml API password bytes.
       * @type {string}
       */
      this.apiPasswordBytes_ = null;

      /*
       * Whether to abort the authentication flow and show an error messagen
       * when content served over an unencrypted connection is detected.
       * @type {boolean}
       */
      this.blockInsecureContent = false;

      this.webviewEventManager_ = WebviewEventManager.create();

      this.webviewEventManager_.addEventListener(
          this.webview_, 'contentload', this.onContentLoad_.bind(this));
      this.webviewEventManager_.addEventListener(
          this.webview_, 'loadabort', this.onLoadAbort_.bind(this));
      this.webviewEventManager_.addEventListener(
          this.webview_, 'permissionrequest',
          this.onPermissionRequest_.bind(this));

      this.webviewEventManager_.addWebRequestEventListener(
          this.webview_.request.onBeforeRequest,
          this.onInsecureRequest.bind(this),
          {urls: ['http://*/*', 'file://*/*', 'ftp://*/*']}, ['blocking']);

      if (!this.startsOnSamlPage_) {
        this.webviewEventManager_.addEventListener(
            this.webview_, 'loadcommit', this.onLoadCommit_.bind(this));

        this.webviewEventManager_.addWebRequestEventListener(
            this.webview_.request.onHeadersReceived,
            this.onHeadersReceived_.bind(this),
            {urls: ['<all_urls>'], types: ['main_frame', 'xmlhttprequest']},
            ['blocking', 'responseHeaders']);
      }

      this.webview_.addContentScripts([{
        name: injectedScriptName,
        matches: ['http://*/*', 'https://*/*'],
        js: {code: injectedJs},
        all_frames: true,
        run_at: 'document_start'
      }]);

      PostMessageChannel.runAsDaemon(this.onConnected_.bind(this));
    }

    /**
     * Whether Saml API is used during auth.
     * @return {boolean}
     */
    get samlApiUsed() {
      return !!this.apiPasswordBytes_;
    }

    /**
     * Returns the Saml API password bytes.
     * @return {string}
     */
    get apiPasswordBytes() {
      return this.apiPasswordBytes_;
    }

    /**
     * Returns the first scraped password if any, or an empty string otherwise.
     * @return {string}
     */
    get firstScrapedPassword() {
      const scraped = this.getConsolidatedScrapedPasswords_();
      return scraped.length ? scraped[0] : '';
    }

    /**
     * Returns the number of scraped passwords.
     * @return {number}
     */
    get scrapedPasswordCount() {
      return this.getConsolidatedScrapedPasswords_().length;
    }

    /**
     * Gets the list of passwords which were scpared exactly |times| times.
     * @return {Array<string>}
     */
    getPasswordsScrapedTimes(times) {
      const passwords = {};
      for (const property in this.passwordStore_) {
        const key = this.passwordStore_[property];
        passwords[key] = (passwords[key] + 1) || 1;
      }
      return Object.keys(passwords).filter(key => passwords[key] == times);
    }

    /**
     * Gets the de-duped scraped passwords.
     * @return {Array<string>}
     * @private
     */
    getConsolidatedScrapedPasswords_() {
      const passwords = {};
      for (const property in this.passwordStore_) {
        passwords[this.passwordStore_[property]] = true;
      }
      return Object.keys(passwords);
    }

    /**
     * Removes the injected content script and unbinds all listeners from the
     * webview passed to the constructor. This SAMLHandler will be unusable
     * after this function returns.
     */
    unbindFromWebview() {
      this.webview_.removeContentScripts([injectedScriptName]);
      this.webviewEventManager_.removeAllListeners();
    }

    /**
     * Resets all auth states
     */
    reset() {
      this.isSamlPage_ = this.startsOnSamlPage_;
      this.pendingIsSamlPage_ = this.startsOnSamlPage_;
      this.passwordStore_ = {};

      this.apiInitialized_ = false;
      this.apiVersion_ = 0;
      this.apiToken_ = null;
      this.apiPasswordBytes_ = null;
    }

    /**
     * Check whether the given |password| is in the scraped passwords.
     * @return {boolean} True if the |password| is found.
     */
    verifyConfirmedPassword(password) {
      return this.getConsolidatedScrapedPasswords_().indexOf(password) >= 0;
    }

    /**
     * Invoked on the webview's contentload event.
     * @private
     */
    onContentLoad_(e) {
      // |this.webview_.contentWindow| may be null after network error screen
      // is shown. See crbug.com/770999.
      if (this.webview_.contentWindow) {
        PostMessageChannel.init(this.webview_.contentWindow);
      } else {
        console.error('SamlHandler.onContentLoad_: contentWindow is null.');
      }
    }

    /**
     * Invoked on the webview's loadabort event.
     * @private
     */
    onLoadAbort_(e) {
      if (e.isTopLevel) {
        this.abortedTopLevelUrl_ = e.url;
      }
    }

    /**
     * Invoked on the webview's loadcommit event for both main and sub frames.
     * @private
     */
    onLoadCommit_(e) {
      // Skip this loadcommit if the top level load is just aborted.
      if (e.isTopLevel && e.url === this.abortedTopLevelUrl_) {
        this.abortedTopLevelUrl_ = null;
        return;
      }

      // Skip for none http/https url.
      if (!e.url.startsWith('https://') && !e.url.startsWith('http://')) {
        return;
      }

      this.isSamlPage_ = this.pendingIsSamlPage_;
    }

    /**
     * Handler for webRequest.onBeforeRequest, invoked when content served over
     * an unencrypted connection is detected. Determines whether the request
     * should be blocked and if so, signals that an error message needs to be
     * shown.
     * @param {Object} details
     * @return {!Object} Decision whether to block the request.
     */
    onInsecureRequest(details) {
      if (!this.blockInsecureContent) {
        return {};
      }
      const strippedUrl = stripParams(details.url);
      this.dispatchEvent(new CustomEvent(
          'insecureContentBlocked', {detail: {url: strippedUrl}}));
      return {cancel: true};
    }

    /**
     * Invoked when headers are received for the main frame.
     * @private
     */
    onHeadersReceived_(details) {
      const headers = details.responseHeaders;

      // Check whether GAIA headers indicating the start or end of a SAML
      // redirect are present.
      for (let i = 0; headers && i < headers.length; ++i) {
        const header = headers[i];
        const headerName = header.name.toLowerCase();

        if (headerName == SAML_HEADER) {
          const action = header.value.toLowerCase();
          if (action == 'start') {
            this.pendingIsSamlPage_ = true;
          } else if (action == 'end') {
            this.pendingIsSamlPage_ = false;
          }
        }
      }

      return {};
    }

    /**
     * Invoked when the injected JS makes a connection.
     */
    onConnected_(port) {
      if (port.targetWindow != this.webview_.contentWindow) {
        return;
      }

      const channel = Channel.create();
      channel.init(port);

      channel.registerMessage('apiCall', this.onAPICall_.bind(this, channel));
      channel.registerMessage(
          'updatePassword', this.onUpdatePassword_.bind(this, channel));
      channel.registerMessage(
          'pageLoaded', this.onPageLoaded_.bind(this, channel));
      channel.registerMessage(
          'getSAMLFlag', this.onGetSAMLFlag_.bind(this, channel));
    }

    sendInitializationSuccess_(channel) {
      channel.send({
        name: 'apiResponse',
        response: {
          result: 'initialized',
          version: this.apiVersion_,
          keyTypes: API_KEY_TYPES
        }
      });
    }

    sendInitializationFailure_(channel) {
      channel.send(
          {name: 'apiResponse', response: {result: 'initialization_failed'}});
    }

    /**
     * Handlers for channel messages.
     * @param {Channel} channel A channel to send back response.
     * @param {Object} msg Received message.
     * @private
     */
    onAPICall_(channel, msg) {
      const call = msg.call;
      if (call.method == 'initialize') {
        if (!Number.isInteger(call.requestedVersion) ||
            call.requestedVersion < MIN_API_VERSION_VERSION) {
          this.sendInitializationFailure_(channel);
          return;
        }

        this.apiVersion_ =
            Math.min(call.requestedVersion, MAX_API_VERSION_VERSION);
        this.apiInitialized_ = true;
        this.sendInitializationSuccess_(channel);
        return;
      }

      if (call.method == 'add') {
        if (API_KEY_TYPES.indexOf(call.keyType) == -1) {
          console.error('SamlHandler.onAPICall_: unsupported key type');
          return;
        }
        // Not setting |email_| and |gaiaId_| because this API call will
        // eventually be followed by onCompleteLogin_() which does set it.
        this.apiToken_ = call.token;
        this.apiPasswordBytes_ = call.passwordBytes;

        this.dispatchEvent(new CustomEvent('apiPasswordAdded'));
      } else if (call.method == 'confirm') {
        if (call.token != this.apiToken_) {
          console.error('SamlHandler.onAPICall_: token mismatch');
        }
      } else {
        console.error('SamlHandler.onAPICall_: unknown message');
      }
    }

    onUpdatePassword_(channel, msg) {
      if (this.isSamlPage_) {
        this.passwordStore_[msg.id] = msg.password;
      }
    }

    onPageLoaded_(channel, msg) {
      this.authDomain = extractDomain(msg.url);
      this.dispatchEvent(new CustomEvent('authPageLoaded', {
        detail: {
          url: msg.url,
          isSAMLPage: this.isSamlPage_,
          domain: this.authDomain
        }
      }));
    }

    onPermissionRequest_(permissionEvent) {
      if (permissionEvent.permission === 'media') {
        // The actual permission check happens in
        // WebUILoginView::RequestMediaAccessPermission().
        this.dispatchEvent(new CustomEvent('videoEnabled'));
        permissionEvent.request.allow();
      }
    }

    onGetSAMLFlag_(channel, msg) {
      return this.isSamlPage_;
    }
  }

  return {SamlHandler: SamlHandler};
});
