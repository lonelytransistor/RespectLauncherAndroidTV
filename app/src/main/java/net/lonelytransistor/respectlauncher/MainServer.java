package net.lonelytransistor.respectlauncher;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.drawable.Drawable;
import android.util.Log;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import dadb.AdbKeyPair;
import dadb.Dadb;

public class MainServer {
    private static final String TAG = "MainServer";
    private static String dataDir = null;
    private static final String SHELL_SERVER_IP = "127.0.0.1";
    private static int SHELL_SERVER_PORT = 6668;

    public static class App {
        String pkgName;
        String activity;
        String name;
        Drawable icon;
        Intent launchIntent;
        Intent leanbackIntent;
    }
    public static Map<String, App> mLaunchers = new HashMap<>();
    public static Map<String, App> mRecents = new HashMap<>();
    public static String mLauncher = "";
    public interface SystemCallback {
        void signal(String stdout, int returnCode);
        void failure();
    }
    public interface SystemCallbackSuccess {
        void signal(String stdout, int returnCode);
    }
    synchronized private static ExecutorService systemPriv(String cmd, SystemCallback cb) {
        Log.i(TAG, "system('" + cmd + "')");
        ExecutorService ex1 = Executors.newSingleThreadExecutor();
        ex1.execute(()->{
            try {
                Socket socket = new Socket(SHELL_SERVER_IP, SHELL_SERVER_PORT);
                OutputStream outputStream = socket.getOutputStream();
                outputStream.write(cmd.getBytes());
                InputStream inputStream = socket.getInputStream();

                StringBuilder responseBuilder = new StringBuilder();
                byte returnCode = -1;
                int byteRead;
                while ((byteRead = inputStream.read()) > 0) {
                    responseBuilder.append((char) byteRead);
                }
                if (byteRead == 0) {
                    returnCode = (byte) inputStream.read();
                }
                socket.close();
                String response = responseBuilder.toString();

                cb.signal(response, returnCode);
            } catch (Exception e) {
                Log.w(TAG, "Exception: ", e);
                cb.failure();
            }
        });
        return ex1;
    }
    public static void system(String cmd, SystemCallback cb, boolean blocking) {
        ExecutorService ex1 = systemPriv(cmd, new SystemCallback() {
            @Override
            public void signal(String stdout, int returnCode) {
                cb.signal(stdout, returnCode);
            }
            @Override
            public void failure() {
                Log.w(TAG, "Restarting adb server");
                startADBServer();
                Log.w(TAG, "Retrying system()");
                systemPriv(cmd, cb);
            }
        });
        if (blocking) {
            try {
                ex1.awaitTermination(30, TimeUnit.SECONDS);
            } catch (Exception ignored){}
        }
    }
    public static void system(String cmd, SystemCallback cb) {
        system(cmd, cb, false);
    }
    public static void system(String cmd, SystemCallbackSuccess cb) {
        system(cmd, new SystemCallback() {
            @Override
            public void signal(String stdout, int returnCode) {
                cb.signal(stdout, returnCode);
            }
            @Override
            public void failure() {
            }
        });
    }
    public static void system(String cmd) {
        system(cmd, (stdout, returnCode) -> {});
    }

    synchronized private static void startADBServer() {
        File binary = new File(dataDir, "lib/respectlauncher.so");
        File priv = new File(dataDir, "adb.priv.key");
        File pub = new File(dataDir, "adb.pub.key");
        AdbKeyPair adbKeyPair;
        try {
            adbKeyPair = AdbKeyPair.read(priv, pub);
        } catch (Exception ignored) {
            return;
        }
        ExecutorService ex1 = Executors.newSingleThreadExecutor();
        ex1.execute(() -> {
            try (Dadb dadb = Dadb.discover("localhost", adbKeyPair)) {
                dadb.push(binary, "/data/local/tmp/respectlauncher", 0700, new Date().getTime());
                ExecutorService ex2 = Executors.newSingleThreadExecutor();
                ex2.execute(() -> {
                    try {
                        SHELL_SERVER_PORT += (int) (Math.random()*10);
                        dadb.shell("/data/local/tmp/respectlauncher -K 172 -UktH -h " + SHELL_SERVER_PORT);
                    } catch (Exception ignored) {}
                });
                try {
                    ex2.awaitTermination(2, TimeUnit.SECONDS);
                } catch (Exception ignored) {}
                Log.i(TAG, "Done.");
            } catch (Exception ignored) {
            }
        });
        try {
            ex1.awaitTermination(4, TimeUnit.SECONDS);
        } catch (Exception ignored) {}
    }
    private static void getAllPackages(Context ctx, Intent intent, Map<String,App> pkgs) {
        PackageManager pm = ctx.getPackageManager();
        List<ResolveInfo> packagesResolve = pm.queryIntentActivities(intent, 0);
        for (ResolveInfo pkg : packagesResolve) {
            App app = new App();
            app.pkgName = pkg.activityInfo.packageName;
            app.activity = pkg.activityInfo.name;
            app.name = String.valueOf(pkg.activityInfo.loadLabel(pm));
            app.icon = pkg.activityInfo.loadIcon(pm);
            app.launchIntent = pm.getLaunchIntentForPackage(app.pkgName);
            app.leanbackIntent = pm.getLeanbackLaunchIntentForPackage(app.pkgName);
            pkgs.put(pkg.activityInfo.packageName, app);
        }
    }
    private static void getAllLaunchers(Context ctx) {
        Intent intent = new Intent(Intent.ACTION_MAIN, null);
        intent.addCategory(Intent.CATEGORY_HOME);
        getAllPackages(ctx, intent, mLaunchers);
    }
    private static void getAllRecents(Context ctx) {
        Intent intent = new Intent("com.android.systemui.TOGGLE_RECENTS", null);
        getAllPackages(ctx, intent, mRecents);
    }
    public static void init(Context ctx, SystemCallback cb) {
        if (dataDir == null)
            dataDir = ctx.getDataDir().getAbsolutePath();
        if (mLaunchers.isEmpty())
            getAllLaunchers(ctx);
        if (mRecents.isEmpty())
            getAllRecents(ctx);
        if (mLauncher.length() < 10) {
            system("cmd shortcut get-default-launcher | grep -o -e '{.*}' | grep -vo -e '[{}]'", new SystemCallback() {
                @Override
                public void signal(String stdout, int returnCode) {
                    mLauncher = stdout.replace("\n", "").replace("\r", "").split("/")[0];
                    cb.signal("", 0);
                }
                @Override
                public void failure() {
                    cb.failure();
                }
            });
        } else {
            cb.signal("", 0);
        }
    }
    public static void init(Context ctx, SystemCallbackSuccess cb) {
        init(ctx, new SystemCallback() {
            @Override
            public void signal(String stdout, int returnCode) {
                cb.signal(stdout, returnCode);
            }
            @Override
            public void failure() {}
        });
    }
    public static void init(Context ctx) {
        init(ctx, new SystemCallback() {
            @Override public void signal(String stdout, int returnCode) {}
            @Override public void failure() {}
        });
    }
}
