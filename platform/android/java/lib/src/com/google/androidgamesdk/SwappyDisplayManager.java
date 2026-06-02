package com.google.androidgamesdk;

import static android.app.NativeActivity.META_DATA_LIB_NAME;

import android.annotation.TargetApi;
import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Display;
import android.view.Window;
import android.view.WindowManager;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class SwappyDisplayManager implements DisplayManager.DisplayListener {
    final private String LOG_TAG = "SwappyDisplayManager";
    final private boolean DEBUG = false;
    final private long ONE_MS_IN_NS = 1000000;
    final private long ONE_S_IN_NS = ONE_MS_IN_NS * 1000;

    private long mCookie;
    private Activity mActivity;
    private DisplayManager mDisplayManager;
    private WindowManager mWindowManager;
    
    // Changed from Display.Mode to Object to avoid Dalvik warnings
    private Object mCurrentMode; 

    private LooperThread mLooper;

    private static class CompatibilityShim {
        @TargetApi(Build.VERSION_CODES.M)
        static Object getMode(Display display) {
            return display.getMode();
        }

        @TargetApi(Build.VERSION_CODES.M)
        static Object[] getSupportedModes(Display display) {
            return display.getSupportedModes();
        }

        @TargetApi(Build.VERSION_CODES.M)
        static boolean modeMatchesCurrentResolution(Object currentModeObj, Object modeObj) {
            Display.Mode currentMode = (Display.Mode) currentModeObj;
            Display.Mode mode = (Display.Mode) modeObj;
            return mode.getPhysicalHeight() == currentMode.getPhysicalHeight()
                    && mode.getPhysicalWidth() == currentMode.getPhysicalWidth();
        }

        @TargetApi(Build.VERSION_CODES.M)
        static float getRefreshRate(Object modeObj) {
            return ((Display.Mode) modeObj).getRefreshRate();
        }

        @TargetApi(Build.VERSION_CODES.M)
        static int getModeId(Object modeObj) {
            return ((Display.Mode) modeObj).getModeId();
        }

        @TargetApi(Build.VERSION_CODES.M)
        static boolean resolutionChanged(Object currentModeObj, Object newModeObj) {
            Display.Mode currentMode = (Display.Mode) currentModeObj;
            Display.Mode newMode = (Display.Mode) newModeObj;
            return (newMode.getPhysicalWidth() != currentMode.getPhysicalWidth())
                    || (newMode.getPhysicalHeight() != currentMode.getPhysicalHeight());
        }

        @TargetApi(Build.VERSION_CODES.LOLLIPOP)
        static void setPreferredDisplayModeId(WindowManager.LayoutParams l, int modeId) {
            l.preferredDisplayModeId = modeId;
        }

        @TargetApi(Build.VERSION_CODES.LOLLIPOP)
        static long getAppVsyncOffsetNanos(Display display) {
            return display.getAppVsyncOffsetNanos();
        }

        @TargetApi(Build.VERSION_CODES.LOLLIPOP)
        static long getPresentationDeadlineNanos(Display display) {
            return display.getPresentationDeadlineNanos();
        }
    }

    private class LooperThread extends Thread {
        public Handler mHandler;
        private Lock mLock = new ReentrantLock();
        private Condition mCondition = mLock.newCondition();

        @Override
        public void start() {
            mLock.lock();
            super.start();
            try {
                mCondition.await();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            mLock.unlock();
        }

        public void run() {
            Log.i(LOG_TAG, "Starting looper thread");

            mLock.lock();
            Looper.prepare();
            mHandler = new Handler();
            mCondition.signal();
            mLock.unlock();

            Looper.loop();

            Log.i(LOG_TAG, "Terminating looper thread");
        }
    }

    // Called from native SwappyDisplayManager.cpp
    public SwappyDisplayManager(long cookie, Activity activity) {
        // Load the native library for cases where an NDK application is running
        // without a java componenet
        try {
            ActivityInfo ai = activity.getPackageManager().getActivityInfo(
                    activity.getIntent().getComponent(), PackageManager.GET_META_DATA);
            if (ai.metaData != null) {
                String nativeLibName = ai.metaData.getString(META_DATA_LIB_NAME);
                if (nativeLibName != null) {
                    System.loadLibrary(nativeLibName);
                }
            }
        } catch (java.lang.Throwable e) {
            Log.e(LOG_TAG, e.getMessage());
        }

        mCookie = cookie;
        mActivity = activity;

        mDisplayManager = (DisplayManager) mActivity.getSystemService(Context.DISPLAY_SERVICE);
        mWindowManager = (WindowManager) mActivity.getSystemService(Context.WINDOW_SERVICE);
        Display display = mWindowManager.getDefaultDisplay();
        
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            mCurrentMode = CompatibilityShim.getMode(display);
            updateSupportedRefreshRates(display);
        }

        // Register display listener callbacks
        synchronized (this) {
            mLooper = new LooperThread();
            mLooper.start();
            mDisplayManager.registerDisplayListener(this, mLooper.mHandler);
        }
    }

    private void updateSupportedRefreshRates(Display display) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Object[] supportedModes = CompatibilityShim.getSupportedModes(display);
            int totalModes = 0;
            for (int i = 0; i < supportedModes.length; i++) {
                if (!CompatibilityShim.modeMatchesCurrentResolution(mCurrentMode, supportedModes[i])) {
                    continue;
                }
                totalModes++;
            }

            long[] supportedRefreshPeriods = new long[totalModes];
            int[] supportedDisplayModeIds = new int[totalModes];
            totalModes = 0;
            for (int i = 0; i < supportedModes.length; i++) {
                if (!CompatibilityShim.modeMatchesCurrentResolution(mCurrentMode, supportedModes[i])) {
                    continue;
                }
                supportedRefreshPeriods[totalModes] =
                        (long) (ONE_S_IN_NS / CompatibilityShim.getRefreshRate(supportedModes[i]));
                supportedDisplayModeIds[totalModes] = CompatibilityShim.getModeId(supportedModes[i]);
                totalModes++;
            }
            // Call down to native to set the supported refresh rates
            nSetSupportedRefreshPeriods(mCookie, supportedRefreshPeriods, supportedDisplayModeIds);
        }
    }

    // Called from native SwappyDisplayManager.cpp
    public void setPreferredDisplayModeId(final int modeId) {
        mActivity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Window w = mActivity.getWindow();
                WindowManager.LayoutParams l = w.getAttributes();
                if (DEBUG) {
                    Log.v(LOG_TAG, "set preferredDisplayModeId to " + modeId);
                }
                
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    CompatibilityShim.setPreferredDisplayModeId(l, modeId);
                }

                w.setAttributes(l);
            }
        });
    }

    // Called from native SwappyDisplayManager.cpp
    public void terminate() {
        mDisplayManager.unregisterDisplayListener(this);
        mLooper.mHandler.getLooper().quit();
    }

    @Override
    public void onDisplayAdded(int displayId) {}

    @Override
    public void onDisplayRemoved(int displayId) {}

    @Override
    public void onDisplayChanged(int displayId) {
        synchronized (this) {
            Display display = mWindowManager.getDefaultDisplay();
            float newRefreshRate = display.getRefreshRate();
            
            boolean resolutionChanged = false;
            boolean refreshRateChanged = false;

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                Object newMode = CompatibilityShim.getMode(display);
                resolutionChanged = CompatibilityShim.resolutionChanged(mCurrentMode, newMode);
                refreshRateChanged = (newRefreshRate != CompatibilityShim.getRefreshRate(mCurrentMode));
                mCurrentMode = newMode;
            } else {
                refreshRateChanged = true; // Fallback to ensure refresh period updates trigger safely on older APIs
            }

            if (resolutionChanged) {
                updateSupportedRefreshRates(display);
            }

            if (refreshRateChanged) {
                long appVsyncOffsetNanos = 0;
                long vsyncPresentationDeadlineNanos = ONE_MS_IN_NS * 16; // Sensible default

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    appVsyncOffsetNanos = CompatibilityShim.getAppVsyncOffsetNanos(display);
                    vsyncPresentationDeadlineNanos =
                            CompatibilityShim.getPresentationDeadlineNanos(mWindowManager.getDefaultDisplay());
                }

                final long vsyncPeriodNanos = (long) (ONE_S_IN_NS / newRefreshRate);
                final long sfVsyncOffsetNanos =
                        vsyncPeriodNanos - (vsyncPresentationDeadlineNanos - ONE_MS_IN_NS);

                nOnRefreshPeriodChanged(
                        mCookie, vsyncPeriodNanos, appVsyncOffsetNanos, sfVsyncOffsetNanos);
            }
        }
    }

    private native void nSetSupportedRefreshPeriods(
            long cookie, long[] refreshPeriods, int[] modeIds);
    private native void nOnRefreshPeriodChanged(
            long cookie, long refreshPeriod, long appOffset, long sfOffset);
}