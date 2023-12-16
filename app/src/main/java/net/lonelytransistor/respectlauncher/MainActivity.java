package net.lonelytransistor.respectlauncher;

import androidx.fragment.app.FragmentActivity;
import android.os.Bundle;

public class MainActivity extends FragmentActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setTheme(R.style.Theme_Leanback_Onboarding);
        setContentView(R.layout.activity_main);
    }
}
