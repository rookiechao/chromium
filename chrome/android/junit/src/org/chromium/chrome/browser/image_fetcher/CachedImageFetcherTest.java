// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.image_fetcher;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyObject;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;

import android.graphics.Bitmap;
import android.support.test.filters.SmallTest;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import org.mockito.invocation.InvocationOnMock;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLooper;

import org.chromium.base.Callback;
import org.chromium.base.task.test.BackgroundShadowAsyncTask;
import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.contextmenu.ChromeContextMenuPopulatorTest.ShadowUrlUtilities;

import jp.tomorrowkey.android.gifplayer.BaseGifImage;

/**
 * Unit tests for CachedImageFetcher.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE,
        shadows = {ShadowUrlUtilities.class, BackgroundShadowAsyncTask.class})
public class CachedImageFetcherTest {
    private static final String UMA_CLIENT_NAME = "TestUmaClient";
    private static final String URL = "http://foo.bar";
    private static final int WIDTH_PX = 100;
    private static final int HEIGHT_PX = 200;

    CachedImageFetcher mCachedImageFetcher;

    @Mock
    ImageFetcherBridge mImageFetcherBridge;
    @Mock
    Bitmap mBitmap;
    @Mock
    BaseGifImage mGif;

    @Captor
    ArgumentCaptor<Callback<Bitmap>> mCallbackCaptor;

    @Before
    public void setUp() throws Exception {
        MockitoAnnotations.initMocks(this);
        mCachedImageFetcher = Mockito.spy(new CachedImageFetcher(mImageFetcherBridge));
        Mockito.doReturn(URL).when(mImageFetcherBridge).getFilePath(anyObject());
        doAnswer((InvocationOnMock invocation) -> {
            mCallbackCaptor.getValue().onResult(mBitmap);
            return null;
        })
                .when(mImageFetcherBridge)
                .fetchImage(anyInt(), eq(URL), eq(UMA_CLIENT_NAME), anyInt(), anyInt(),
                        mCallbackCaptor.capture());
    }

    @Test
    @SmallTest
    public void testFetchImageWithDimensionsFoundOnDisk() throws Exception {
        Mockito.doReturn(mBitmap).when(mCachedImageFetcher).tryToLoadImageFromDisk(anyObject());
        mCachedImageFetcher.fetchImage(URL, UMA_CLIENT_NAME, WIDTH_PX, HEIGHT_PX,
                (Bitmap bitmap) -> { assertEquals(bitmap, mBitmap); });
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mCachedImageFetcher)
                .fetchImageImpl(eq(URL), eq(UMA_CLIENT_NAME), eq(WIDTH_PX), eq(HEIGHT_PX), any());
        verify(mImageFetcherBridge, never()) // Should never make it to native.
                .fetchImage(anyInt(), eq(URL), eq(UMA_CLIENT_NAME), anyInt(), anyInt(), any());

        // Verify metrics have been reported.
        verify(mImageFetcherBridge)
                .reportEvent(eq(UMA_CLIENT_NAME), eq(CachedImageFetcherEvent.JAVA_DISK_CACHE_HIT));
        verify(mImageFetcherBridge).reportCacheHitTime(eq(UMA_CLIENT_NAME), anyLong());
    }

    @Test
    @SmallTest
    public void testFetchImageWithDimensionsCallToNative() throws Exception {
        Mockito.doReturn(null).when(mCachedImageFetcher).tryToLoadImageFromDisk(anyObject());
        mCachedImageFetcher.fetchImage(URL, UMA_CLIENT_NAME, WIDTH_PX, HEIGHT_PX,
                (Bitmap bitmap) -> { assertEquals(bitmap, mBitmap); });
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mCachedImageFetcher)
                .fetchImageImpl(eq(URL), eq(UMA_CLIENT_NAME), eq(WIDTH_PX), eq(HEIGHT_PX), any());
        verify(mImageFetcherBridge)
                .fetchImage(anyInt(), eq(URL), eq(UMA_CLIENT_NAME), anyInt(), anyInt(), any());
    }

    @Test
    @SmallTest
    public void testFetchImageWithNoDimensionsFoundOnDisk() throws Exception {
        Mockito.doReturn(mBitmap).when(mCachedImageFetcher).tryToLoadImageFromDisk(anyObject());
        mCachedImageFetcher.fetchImage(
                URL, UMA_CLIENT_NAME, (Bitmap bitmap) -> { assertEquals(bitmap, mBitmap); });
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mCachedImageFetcher)
                .fetchImageImpl(eq(URL), eq(UMA_CLIENT_NAME), eq(0), eq(0), any());
        verify(mImageFetcherBridge, never())
                .fetchImage(anyInt(), eq(URL), eq(UMA_CLIENT_NAME), anyInt(), anyInt(), any());
    }

    @Test
    @SmallTest
    public void testFetchImageWithNoDimensionsCallToNative() throws Exception {
        Mockito.doReturn(null).when(mCachedImageFetcher).tryToLoadImageFromDisk(anyObject());
        mCachedImageFetcher.fetchImage(
                URL, UMA_CLIENT_NAME, (Bitmap bitmap) -> { assertEquals(bitmap, mBitmap); });
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mCachedImageFetcher)
                .fetchImageImpl(eq(URL), eq(UMA_CLIENT_NAME), eq(0), eq(0), any());
        verify(mImageFetcherBridge)
                .fetchImage(anyInt(), eq(URL), eq(UMA_CLIENT_NAME), anyInt(), anyInt(), any());
    }

    @Test
    @SmallTest
    public void testFetchTwoClients() throws Exception {
        Mockito.doReturn(null).when(mCachedImageFetcher).tryToLoadImageFromDisk(anyObject());
        mCachedImageFetcher.fetchImage(
                URL, UMA_CLIENT_NAME, (Bitmap bitmap) -> { assertEquals(bitmap, mBitmap); });
        mCachedImageFetcher.fetchImage(
                URL, UMA_CLIENT_NAME + "2", (Bitmap bitmap) -> { assertEquals(bitmap, mBitmap); });
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mCachedImageFetcher)
                .fetchImageImpl(eq(URL), eq(UMA_CLIENT_NAME), eq(0), eq(0), any());
        verify(mCachedImageFetcher)
                .fetchImageImpl(eq(URL), eq(UMA_CLIENT_NAME + "2"), eq(0), eq(0), any());
        verify(mImageFetcherBridge)
                .fetchImage(anyInt(), eq(URL), eq(UMA_CLIENT_NAME), anyInt(), anyInt(), any());
        verify(mImageFetcherBridge)
                .fetchImage(
                        anyInt(), eq(URL), eq(UMA_CLIENT_NAME + "2"), anyInt(), anyInt(), any());
    }

    @Test
    @SmallTest
    public void testFetchGifFoundOnDisk() throws Exception {
        Mockito.doReturn(mGif).when(mCachedImageFetcher).tryToLoadGifFromDisk(anyObject());
        mCachedImageFetcher.fetchGif(
                URL, UMA_CLIENT_NAME, (BaseGifImage gif) -> { assertEquals(gif, mGif); });
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mImageFetcherBridge, never()) // Should never make it to native.
                .fetchGif(eq(URL), eq(UMA_CLIENT_NAME), any());

        // Verify metrics have been reported.
        verify(mImageFetcherBridge)
                .reportEvent(eq(UMA_CLIENT_NAME), eq(CachedImageFetcherEvent.JAVA_DISK_CACHE_HIT));
        verify(mImageFetcherBridge).reportCacheHitTime(eq(UMA_CLIENT_NAME), anyLong());
    }

    @Test
    @SmallTest
    public void testFetchGifCallToNative() throws Exception {
        Mockito.doReturn(null).when(mCachedImageFetcher).tryToLoadGifFromDisk(anyObject());
        mCachedImageFetcher.fetchGif(URL, UMA_CLIENT_NAME, (BaseGifImage gif) -> {});
        BackgroundShadowAsyncTask.runBackgroundTasks();
        ShadowLooper.runUiThreadTasks();

        verify(mImageFetcherBridge).fetchGif(eq(URL), eq(UMA_CLIENT_NAME), any());
    }
}
