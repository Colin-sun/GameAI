package com.aiprojects.gameai

import com.google.android.material.chip.Chip
import android.content.Intent
import android.graphics.Color
import android.graphics.PorterDuff
import android.os.Bundle
import android.view.MenuItem
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.google.android.material.floatingactionbutton.FloatingActionButton

class UTTHumanPlayerSelect : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_utthuman_player_select)
        // 显示返回箭头（←）
        setSupportActionBar(findViewById(R.id.toolbar))
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        // 把箭头刷成白色
        val whiteArrow = ContextCompat.getDrawable(this, R.drawable.baseline_arrow_back_24)
        whiteArrow?.setColorFilter(Color.WHITE, PorterDuff.Mode.SRC_ATOP)
        supportActionBar?.setHomeAsUpIndicator(whiteArrow)

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
            startActivity(setting_intent)        // 启动 settingActivity
        }

        // 绑定玩家选择跳转
        val buttonAIFirstSelect = findViewById<Chip>(R.id.chipAIFirst)
        buttonAIFirstSelect.setOnClickListener {
            val intent = Intent(this, UTTAIvsHuman::class.java)
            intent.putExtra("mode", "AI_FIRST")  // 传递模式参数
            startActivity(intent)
        }

        val buttonAISecondSelect = findViewById<Chip>(R.id.chipAISecond)
        buttonAISecondSelect.setOnClickListener {
             val intent = Intent(this, UTTAIvsHuman::class.java)
             intent.putExtra("mode", "AI_SECOND")  // 传递模式参数
             startActivity(intent)
        }

    }
    // 让箭头能点
    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            finish()
            return true
        }
        return super.onOptionsItemSelected(item)
    }
}