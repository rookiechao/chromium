// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr;

import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.modules.ModuleInstallUi;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.module_installer.OnModuleInstallFinishedListener;

import java.util.ArrayList;
import java.util.List;

/**
 * Instantiates the VR delegates. If the VR module is not available this provider will
 * instantiate a fallback implementation.
 */
@JNINamespace("vr")
public class VrModuleProvider implements ModuleInstallUi.FailureUiListener {
    private static VrDelegateProvider sDelegateProvider;
    private static final List<VrModeObserver> sVrModeObservers = new ArrayList<>();
    private static boolean sAlwaysUseFallbackDelegate;

    private long mNativeVrModuleProvider;
    private Tab mTab;

    /**
     * Need to be called after native libraries are available. Has no effect if VR is not compiled
     * into Chrome.
     */
    public static void maybeInit() {
        if (!VrBuildConfig.IS_VR_ENABLED) return;
        nativeInit();
        // Always install the VR module on Daydream-ready devices.
        maybeRequestModuleIfDaydreamReady();
    }

    /**
     * Requests deferred installation of the VR module on Daydream-ready devices. Has no effect if
     * VR is not compiled into Chrome.
     */
    public static void maybeRequestModuleIfDaydreamReady() {
        if (!VrBuildConfig.IS_VR_ENABLED) return;
        if (VrModule.isInstalled()) return;
        if (!getDelegate().isDaydreamReadyDevice()) return;

        // Installs module when on unmetered network connection and device is charging.
        VrModule.installDeferred();
    }

    public static VrDelegate getDelegate() {
        return getDelegateProvider().getDelegate();
    }

    public static VrIntentDelegate getIntentDelegate() {
        return getDelegateProvider().getIntentDelegate();
    }

    /**
     * Registers the given {@link VrModeObserver}.
     *
     * @param observer The VrModeObserver to register.
     */
    public static void registerVrModeObserver(VrModeObserver observer) {
        sVrModeObservers.add(observer);
    }

    /**
     * Unregisters the given {@link VrModeObserver}.
     *
     * @param observer The VrModeObserver to remove.
     */
    public static void unregisterVrModeObserver(VrModeObserver observer) {
        sVrModeObservers.remove(observer);
    }

    public static void onEnterVr() {
        for (VrModeObserver observer : sVrModeObservers) observer.onEnterVr();
    }

    public static void onExitVr() {
        for (VrModeObserver observer : sVrModeObservers) observer.onExitVr();
    }

    /* package */ static void installModule(OnModuleInstallFinishedListener onFinishedListener) {
        VrModule.install((success) -> {
            if (success) {
                // Re-create delegate provider.
                sDelegateProvider = null;
                VrDelegate delegate = getDelegate();
                assert !(delegate instanceof VrDelegateFallback);
                delegate.initAfterModuleInstall();
            }
            onFinishedListener.onFinished(success);
        });
    }

    // TODO(crbug.com/870055): JNI should be registered in the shared VR library's JNI_OnLoad
    // function. Do this once we have a shared VR library.
    /* package */ static void registerJni() {
        nativeRegisterJni();
    }

    private static VrDelegateProvider getDelegateProvider() {
        if (sDelegateProvider == null) {
            if (!VrModule.isInstalled()) {
                sDelegateProvider = new VrDelegateProviderFallback();
            } else {
                sDelegateProvider = VrModule.getImpl();
            }
        }
        return sDelegateProvider;
    }

    @CalledByNative
    private static VrModuleProvider create(long nativeVrModuleProvider) {
        return new VrModuleProvider(nativeVrModuleProvider);
    }

    @CalledByNative
    private static boolean isModuleInstalled() {
        return VrModule.isInstalled();
    }

    @Override
    public void onRetry() {
        if (mNativeVrModuleProvider != 0) {
            installModule(mTab);
        }
    }

    @Override
    public void onCancel() {
        if (mNativeVrModuleProvider != 0) {
            nativeOnInstalledModule(mNativeVrModuleProvider, false);
        }
    }

    private VrModuleProvider(long nativeVrModuleProvider) {
        mNativeVrModuleProvider = nativeVrModuleProvider;
    }

    @CalledByNative
    private void onNativeDestroy() {
        mNativeVrModuleProvider = 0;
    }

    @CalledByNative
    private void installModule(Tab tab) {
        mTab = tab;
        ModuleInstallUi ui = new ModuleInstallUi(mTab, R.string.vr_module_title, this);
        ui.showInstallStartUi();
        installModule((success) -> {
            if (mNativeVrModuleProvider != 0) {
                if (!success) {
                    ui.showInstallFailureUi();
                    return;
                }
                ui.showInstallSuccessUi();
                nativeOnInstalledModule(mNativeVrModuleProvider, success);
            }
        });
    }

    private static native void nativeInit();
    private static native void nativeRegisterJni();
    private native void nativeOnInstalledModule(long nativeVrModuleProvider, boolean success);
}
