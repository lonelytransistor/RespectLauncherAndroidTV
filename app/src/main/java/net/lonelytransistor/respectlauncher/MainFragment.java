package net.lonelytransistor.respectlauncher;

import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.view.View;

import androidx.annotation.NonNull;

import net.lonelytransistor.commonlib.TVSettingsFragment;
import net.lonelytransistor.commonlib.Utils;

import java.util.Arrays;

public class MainFragment extends TVSettingsFragment {
    private static final String TAG = "MainFragment";

    @Override
    public void onDetach() {
        super.onDetach();
    }
    @Override
    public void onAttach(@NonNull Context context) {
        super.onAttach(context);
    }
    @Override
    public void onBuild(View view) {
        setSubtitle(MainServer.mLauncher);
        setTitle(net.lonelytransistor.commonlib.R.string.settings);
        setDescription(net.lonelytransistor.commonlib.R.string.general_settings);
        setIcon(net.lonelytransistor.commonlib.R.drawable.settings);

        Label launchers = new Label()
                .setIcon(Utils.getDrawable(getContext(), R.drawable.rocket, android.R.color.white))
                .setTitle("Launcher")
                .setDescription("Home screen apps");
        RadioGroup launchersRadio = new RadioGroup((group, active, value) -> {
            MainServer.system("cmd package set-home-activity " + ((MainServer.App) value).pkgName);
        });
        for (MainServer.App launcher : MainServer.mLaunchers.values()) {
            RadioGroup.Radio radio = launchersRadio.addItem()
                    .setTitle(launcher.name)
                    .setIcon(launcher.icon)
                    .setValue(launcher);
            if (MainServer.mLauncher.equals(launcher.pkgName)) {
                launchersRadio.active = radio;
            }
        }
        addPanelAction(launchers, launchersRadio);
        addAction(launchers);

        Label recents = new Label()
                .setIcon(Utils.getDrawable(getContext(), R.drawable.layers, android.R.color.white))
                .setTitle("Recents")
                .setDescription("Recent apps UI");
        RadioGroup recentsRadio = new RadioGroup((group, active, value) -> {
            MainServer.system("cmd package set-home-activity " + value);
        });
        for (MainServer.App recent : MainServer.mRecents.values()) {
            RadioGroup.Radio radio = launchersRadio.addItem()
                    .setTitle(recent.name)
                    .setIcon(recent.icon)
                    .setValue(recent.pkgName);
            if (MainServer.mLauncher.equals(recent.pkgName)) {
                launchersRadio.active = radio;
            }
        }
        addPanelAction(recents, recentsRadio);
        addAction(recents);

        PackageManager pm = getContext().getPackageManager();
        ComponentName launcherComponent = new ComponentName(getContext(), LauncherActivity.class);
        Label launcherIcon = new Label()
                .setIcon(Utils.getDrawable(getContext(), R.drawable.android, android.R.color.white))
                .setTitle("Launcher icon")
                .setDescription("Show/hide this app from launchers");
        RadioGroup launcherIconRadio = new RadioGroup((group, active, value) -> {
                MainServer.system("pm " +
                        (((Boolean) value) ? "enable" : "disable") + " " +
                        getContext().getPackageName() + "/." + LauncherActivity.class.getSimpleName() +
                        " && sleep 1 && " +
                        "am start " + getContext().getPackageName() + "/." + MainActivity.class.getSimpleName());
        });
        RadioGroup.Radio launcherIconRadioYes = launcherIconRadio.addItem()
                .setTitle("Show")
                .setValue(true);
        RadioGroup.Radio launcherIconRadioNo = launcherIconRadio.addItem()
                .setTitle("Hide")
                .setValue(false);
        if (Arrays.asList(
                PackageManager.COMPONENT_ENABLED_STATE_DEFAULT,
                PackageManager.COMPONENT_ENABLED_STATE_ENABLED)
                .contains(pm.getComponentEnabledSetting(launcherComponent))) {
            launcherIconRadio.active = launcherIconRadioYes;
        } else {
            launcherIconRadio.active = launcherIconRadioNo;
        }
        addPanelAction(launcherIcon, launcherIconRadio);
        launcherIcon.addPanelAction(new Label()
                .setTitle("If hidden use 'configuration'")
                .setDescription("It's within the Accessibility Settings")
                .setFocusable(false));
        addAction(launcherIcon);
    }
}
