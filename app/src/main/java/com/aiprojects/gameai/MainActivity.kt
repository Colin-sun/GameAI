package com.aiprojects.gameai

import android.content.Intent
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import androidx.activity.enableEdgeToEdge
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.google.android.material.floatingactionbutton.FloatingActionButton
import com.google.android.material.chip.Chip

// 启动界面
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        // 设置布局
        setContentView(R.layout.activity_main)
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }
        // 绑定设置跳转
        // 找到设置按钮
        val buttonSetting = findViewById<FloatingActionButton>(R.id.buttonSetting)
        buttonSetting.setOnClickListener {
            // 显式 Intent
            // Intent 是 Android 中用于 请求某种操作 的消息对象，是 组件间通信 的核心机制。
            val setting_intent = Intent(this, SettingActivity::class.java)
            startActivity(setting_intent)        // 启动
        }

        // 绑定UTT模式设置跳转
        val buttonUTTMode = findViewById<Chip>(R.id.chipUTTModeSelect)
        buttonUTTMode.setOnClickListener {
            val utt_mode_intent = Intent(this, UTTModeSelect::class.java)
            startActivity(utt_mode_intent)
        }


    }
}

//fun test(){
//    // 获取空盘的best_move并显示
//    val gamePtr = GamePtr.create(IntArray(9 * 9), -1, -1)
//    val mctsPurePtr = MCTSPurePtr.create(600, 0.8f)
//    val (row, col) = mctsPurePtr.getBestMove(gamePtr)
//    val text = "Best Move: ($row, $col)"
//}