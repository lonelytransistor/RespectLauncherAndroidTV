package net.lonelytransistor.respectlauncher;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.media.AudioManager;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.VideoView;

import androidx.annotation.Nullable;
import androidx.leanback.app.GuidedStepSupportFragment;
import androidx.leanback.app.OnboardingSupportFragment;
import androidx.leanback.widget.PagingIndicator;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

import dadb.AdbKeyPair;
import dadb.Dadb;

public class IntroFragment extends OnboardingSupportFragment {
    private static final String TAG = "IntroFragment";

    @Override
    protected CharSequence getPageTitle(int pageIndex) {
        switch (pageIndex) {
            case 0:
                return "Hello";
            case 1:
                return "Enable debugging";
            case 3:
                return "Pair the device";
            case 5:
                return "Enable accessibility service";
        }
        return null;
    }
    @Override
    protected CharSequence getPageDescription(int pageIndex) {
        switch (pageIndex) {
            case 1:
                return "In order for the app work, USB or wireless Debugging must be switched on in Developer Settings.\n" +
                        "In a second development settings will open.\n" +
                        "There you should turn on first developer options, and then debugging as presented on the screen capture.";
            case 3:
                return "The app has to pair with the Android Debug Bridge of this device in order to access low level key events.\n" +
                        "In a second a window will appear, where you should select Always allow as presented on the screen capture.";
            case 5:
                return "The app now has to be enabled as an accessibility service.\n" +
                        "In a moment accessibility settings will open, where you should enable this application as presented on the screen capture.";
        }
        return null;
    }
    protected String getPageVideo(int pageIndex) {
        switch (pageIndex) {
            case 1:
                return "android.resource://" + getContext().getPackageName() + "/" + R.raw.devops;
            case 3:
                return "android.resource://" + getContext().getPackageName() + "/" + R.raw.pair;
            case 5:
                return "android.resource://" + getContext().getPackageName() + "/" + R.raw.service;
        }
        return null;
    }
    @Override
    protected int getPageCount() {
        return 8;
    }

    private void pairADB(Context ctx, SharedPreferences sharedPreferences) {
        Executors.newSingleThreadExecutor().execute(()-> {
            String dataDir = ctx.getDataDir().getAbsolutePath();

            AdbKeyPair adbKeyPair = null;
            File priv = new File(dataDir, "adb.priv.key");
            File pub = new File(dataDir, "adb.pub.key");
            try {
                adbKeyPair = AdbKeyPair.read(priv, pub);
            } catch (Exception ignored) {}
            if (adbKeyPair == null) {
                AdbKeyPair.generate(priv, pub);
                adbKeyPair = AdbKeyPair.read(priv, pub);
            }

            try (Dadb ignored1 = Dadb.discover("localhost", adbKeyPair)) {
                MainServer.init(ctx, new MainServer.SystemCallback() {
                    @Override
                    public void signal(String stdout, int returnCode) {
                        SharedPreferences.Editor prefs = sharedPreferences.edit();
                        prefs.putBoolean("ADB_PAIRED", true);
                        prefs.apply();
                        executor.execute(() -> moveToNextPage());
                    }
                    @Override
                    public void failure() {
                        SharedPreferences.Editor prefs = sharedPreferences.edit();
                        prefs.putBoolean("ADB_PAIRED", false);
                        prefs.apply();
                    }
                });
            } catch (Exception e) {
                Log.e(TAG, "Failed.", e);
            }
            SharedPreferences.Editor prefs = sharedPreferences.edit();
            prefs.putBoolean("ADB_PAIRED", false);
            prefs.apply();
        });
    }
    private VideoView videoView;
    private Executor executor;
    protected void updateVideoView(int pageIndex) {
        if (videoView == null)
            return;
        String url = getPageVideo(pageIndex);
        if (url != null) {
            videoView.setVideoPath(url);
            videoView.start();
            videoView.setVisibility(View.VISIBLE);
        } else {
            videoView.setVisibility(View.INVISIBLE);
            videoView.stopPlayback();
        }
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode % 2 == 0) {
            moveToPreviousPage();
        }
        tryStartActivityNext();
    }
    private long tryStartActivityTime = 0;
    private final List<Intent> tryStartActivityIntents = new ArrayList<>();
    private void tryStartActivityNext() {
        long timeNow = System.currentTimeMillis();
        Log.i(TAG, tryStartActivityTime + " : " + timeNow);
        if (tryStartActivityTime+10*1000 > timeNow && !tryStartActivityIntents.isEmpty()) {
            tryStartActivityTime = timeNow;
            startActivityForResult(tryStartActivityIntents.remove(0), 0);
        } else {
            tryStartActivityIntents.clear();
            tryStartActivityTime = 0;
        }
    }
    private void tryStartActivity(String... actions) {
        PackageManager pm = getContext().getPackageManager();
        tryStartActivityIntents.clear();
        for (String action : actions) {
            Intent intent = new Intent(action);
            List<ResolveInfo> activities = pm.queryIntentActivities(intent, 0);

            if (!activities.isEmpty()) {
                tryStartActivityIntents.add(intent);
            }
        }
        tryStartActivityTime = System.currentTimeMillis();
        tryStartActivityNext();
    }
    @Override
    protected void onFinishFragment() {
        super.onFinishFragment();
        GuidedStepSupportFragment.addAsRoot(getActivity(), new MainFragment(), android.R.id.content);
    }
    @Override
    protected void onPageChanged(int newPage, int previousPage) {
        super.onPageChanged(newPage, previousPage);
        updateVideoView(newPage);

        SharedPreferences sharedPreferences = PreferenceManager.getDefaultSharedPreferences(getContext());
        boolean adbPaired = sharedPreferences.getBoolean("ADB_PAIRED", false);
        boolean adbEnabled = Settings.Global.getInt(getContext().getContentResolver(), Settings.Global.ADB_ENABLED, 0) == 1;
        String prefString = Settings.Secure.getString(getContext().getContentResolver(), Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES);
        Log.i(TAG, "str: " + prefString);
        boolean accessibilityOn = prefString != null && prefString.contains(getContext().getPackageName() + "/" + ActivityMonitor.class.getName());
        switch (newPage) {
            case 1:
                if (adbEnabled) {
                    moveToNextPage();
                }
                break;
            case 2:
                if (adbEnabled) {
                    moveToNextPage();
                } else {
                    tryStartActivity(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS,
                            "com.android.settings.APPLICATION_DEVELOPMENT_SETTINGS");
                }
                break;
            case 3:
                if (adbPaired) {
                    moveToNextPage();
                }
                break;
            case 4:
                if (adbPaired) {
                    moveToNextPage();
                } else {
                    pairADB(getContext(), sharedPreferences);
                }
                break;
            case 5:
                if (accessibilityOn) {
                    moveToNextPage();
                }
                break;
            case 6:
                if (accessibilityOn) {
                    moveToNextPage();
                } else {
                    tryStartActivity(Settings.ACTION_ACCESSIBILITY_SETTINGS,
                            "android.settings.ACCESSIBILITY_TV_OEM_LINK");
                }
                break;
            case 7:
                MainServer.init(getContext(), (stdout, returnCode) -> onFinishFragment());
                break;
        }
    }
    @Nullable
    @Override
    protected View onCreateBackgroundView(LayoutInflater inflater, ViewGroup container) {
        return null;
    }
    @Nullable
    @Override
    protected View onCreateContentView(LayoutInflater inflater, ViewGroup container) {
        Context ctx = getContext();
        videoView = new VideoView(ctx);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.MATCH_PARENT);
        lp.gravity = Gravity.CENTER;
        videoView.setLayoutParams(lp);
        videoView.setAudioFocusRequest(AudioManager.AUDIOFOCUS_NONE);
        videoView.setMediaController(null);
        videoView.setOnCompletionListener(mp -> videoView.start());
        videoView.setFocusable(false);
        videoView.setFocusableInTouchMode(false);
        videoView.setFocusedByDefault(false);
        videoView.setScreenReaderFocusable(false);
        updateVideoView(getCurrentPageIndex());
        return videoView;
    }
    @Nullable
    @Override
    protected View onCreateForegroundView(LayoutInflater inflater, ViewGroup container) {
        return null;
    }

    @Nullable
    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        View v = super.onCreateView(inflater, container, savedInstanceState);

        executor = getContext().getMainExecutor();

        PagingIndicator pageIndicator = v.findViewById(androidx.leanback.R.id.page_indicator);
        pageIndicator.setFocusable(false);
        Button startButton = v.findViewById(androidx.leanback.R.id.button_start);
        startButton.setFocusable(false);
        v.setOnClickListener(v1 -> {
            if (getPageTitle(getCurrentPageIndex()) != null) {
                moveToNextPage();
            }
            if (getCurrentPageIndex() == getPageCount()-1) {
                MainServer.init(getContext(), (stdout, returnCode) -> onFinishFragment());
            }
        });
        return v;
    }
}
