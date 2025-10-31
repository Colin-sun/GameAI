package com.aiprojects.gameai

import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import android.content.Context
import android.graphics.Color
import android.graphics.PorterDuff
import android.os.Bundle
import android.util.AttributeSet
import android.util.Log
import android.view.MenuItem
import android.view.View
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceViewHolder
import androidx.recyclerview.widget.RecyclerView
import androidx.preference.EditTextPreference
import android.text.InputType
import android.text.method.DigitsKeyListener
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AlertDialog
import androidx.core.content.edit
import androidx.core.graphics.drawable.DrawableCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.preference.PreferenceManager
import androidx.preference.Preference

class SettingActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.settings_activity)
        // 显示返回箭头（←）
        setSupportActionBar(findViewById(R.id.toolbar))
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        // 把箭头刷成白色
        val whiteArrow = ContextCompat.getDrawable(this, R.drawable.baseline_arrow_back_24)
        whiteArrow?.let {
            DrawableCompat.setTint(it, Color.WHITE)
        }
        supportActionBar?.setHomeAsUpIndicator(whiteArrow)

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        // 加载设置选项
        if (savedInstanceState == null) {
            supportFragmentManager
                .beginTransaction()
                .replace(R.id.settings_container, SettingsFragment())
                .commit()
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

    // 加载设置项，同时清掉最后的空白，便于自定义间距
    class SettingsFragment : PreferenceFragmentCompat() {
        private lateinit var nPlayoutPreference: EditTextPreference
        private lateinit var cPuctPreference: EditTextPreference
        private var nPlayoutText: String? = null   // 用于手动同步
        private var cPuctText: String? = null
        override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
            Log.d("SettingsFragment", "onCreatePreferences called")
            setPreferencesFromResource(R.xml.root_preferences, rootKey)

            nPlayoutPreference = findPreference("mcts_pure_n_playout")!!
            // 弹输入框前把 nPlayoutText 写进去
            nPlayoutPreference.setOnBindEditTextListener { editText ->
                nPlayoutText?.let { editText.setText(it) }
            }

            cPuctPreference = findPreference("mcts_pure_c_puct")!!
            cPuctPreference.setOnBindEditTextListener { editText ->
                cPuctText?.let { editText.setText(it) }
            }


            // 限制 n_playout 输入范围
            // 找到 n_playout_pref; ?: return: 如果 findPreference 返回 null，则返回
            val n_playout_pref = findPreference<EditTextPreference>("mcts_pure_n_playout") ?: return
            n_playout_pref.setOnBindEditTextListener {editText ->
                // 禁止负号、小数点粘贴
                editText.inputType = InputType.TYPE_CLASS_NUMBER
                editText.keyListener = DigitsKeyListener.getInstance("0123456789")

                // 当用户点“确定”时，先走这里，再走 onPreferenceChangeListener
                // 所以我们把超限判断放在 onPreferenceChangeListener 里，弹 Dialog
            }
            // 保存前校验
            n_playout_pref.setOnPreferenceChangeListener { _, newValue ->
                // 解析 + 合法性校验
                val newInt = newValue.toString().toIntOrNull() ?.takeIf { it > 0 }
                if (newInt == null) {          // 空串、非数字、≤0 都会走到这里
                    // 弹窗提示
                    AlertDialog.Builder(requireContext())
                        .setTitle("输入无效")
                        .setMessage("请输入小于32位int的正整数！")
                        .setPositiveButton("确认"){ _, _ ->
                            nPlayoutText = nPlayoutPreference.text // 恢复原值
                        }
                        .show()
                    return@setOnPreferenceChangeListener false   // 拒绝保存
                }
                // 过大则弹出确认 Dialog，拦截系统默认落盘
                if (newInt > 10000) {
                    // 弹出确认 Dialog
                    AlertDialog.Builder(requireContext())
                        .setTitle("MCTS 模拟次数过大")
                        .setMessage("MCTS 模拟次数过大，严重增加耗时同时棋力难以提升\n" +
                                "确定要设置为 $newInt 吗？")
                        .setPositiveButton("确认") { _, _ ->
                            // 用户确认，手动落盘为 String
                            PreferenceManager.getDefaultSharedPreferences(requireContext())
                                .edit { putString("mcts_pure_n_playout", newInt.toString()) }
                            // 同步 UI 显示的值
                            n_playout_pref.text = newInt.toString()
                            nPlayoutText = newInt.toString()   // 下次弹框时写入
                        }
                        .setNegativeButton("取消"){ _, _ ->
                            nPlayoutText = nPlayoutPreference.text // 恢复原值
                        }
                        .show()
                    false
                } else {
                    // 范围内，系统默认落盘为 String
                    true
                }
            }

            // 限制 c_puct 输入范围
            val c_puct_pref = findPreference<EditTextPreference>("mcts_pure_c_puct") ?: return
            // 键盘只出小数数字，禁止负号
            c_puct_pref.setOnBindEditTextListener { editText ->
                editText.inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL
                // 允许 0-9 和 .
                editText.keyListener = DigitsKeyListener.getInstance("0123456789.")
            }
            // 保存前校验：必须是 >=0 的合法小数
            c_puct_pref.setOnPreferenceChangeListener { _, newValue ->
                val str = newValue.toString()
                // 正则：非负小数，最多 4 位小数
                val regex = Regex("""^\d+(\.\d{1,4})?$""")
                if (!regex.matches(str) || str.toFloat() <= 0 || str.toFloat() > 1000) {
                    // 弹窗提示输入值非法
                    AlertDialog.Builder(requireContext())
                        .setTitle("输入无效")
                        .setMessage("请输入非负小数 \n" +
                                "（最多 4 位小数，不能省略小数点前的0，不能使用科学计数法，最大1000）")
                        .setPositiveButton("确认"){ _, _ ->
                            cPuctText = cPuctPreference.text // 恢复原值
                        }
                        .show()

                    return@setOnPreferenceChangeListener false
                }
                // 范围内，系统默认落盘为 String
                true
            }

            // 点击自动复制最新版链接并提示
            val autoCopyLink = findPreference<Preference>("auto_copy_link")
            autoCopyLink?.setOnPreferenceClickListener {
                // 复制链接
                val clipboard = requireContext().getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
                val clip = ClipData.newPlainText("label", "https://github.com/aiprojects/gameai")
                clipboard.setPrimaryClip(clip)
                // 弹窗提示
                Toast.makeText(requireContext(), "已复制最新版链接", Toast.LENGTH_SHORT).show()
                true
            }
        }

        override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
            super.onViewCreated(view, savedInstanceState)
            // 让整行（recyclerView）的左右 padding 清零，便于自定义间距
            val recyclerView = view.findViewById<RecyclerView>(androidx.preference.R.id.recycler_view)
            recyclerView?.apply {
                setPadding(0, paddingTop, 0, paddingBottom)
                clipToPadding = false
            }
        }
    }
}

/**
 * 自定义 preference category 的样式
 * 直接把样式数据写代码里，因为写 xml 里面仍然需要单独的文件
 */
class PurpleCategory @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : PreferenceCategory(context, attrs) {

    override fun onBindViewHolder(holder: PreferenceViewHolder) {
        super.onBindViewHolder(holder)
        val title = holder.findViewById(android.R.id.title) as TextView // 获取标题
        // 设置标题样式
        title.setTextColor(context.getColor(R.color.purple_500))
        title.textSize = 20f

        // 设置整行（itemView）的左右 padding
        holder.itemView.setPadding(100, holder.itemView.paddingTop,
            0, holder.itemView.paddingBottom)
    }
}