package com.aiprojects.gameai

import android.graphics.Color
import android.graphics.PorterDuff
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.Toast
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.preference.PreferenceManager
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import androidx.appcompat.widget.Toolbar

class UTTAIvsHuman : AppCompatActivity() {
    private lateinit var webView: WebView
    private var currentGameUrl = "" // 当前游戏URL
    private var AIPlayerNum: Int = 1 // AI玩家编号
    private lateinit var webClient: GameWebViewClient

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_uttaivs_human)
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }

        // 显示返回箭头（←）
        setSupportActionBar(findViewById(R.id.toolbar))
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        // 把箭头刷成白色
        val whiteArrow = ContextCompat.getDrawable(this, R.drawable.baseline_arrow_back_24)
        whiteArrow?.setColorFilter(Color.WHITE, PorterDuff.Mode.SRC_ATOP)
        supportActionBar?.setHomeAsUpIndicator(whiteArrow)

        webView = findViewById(R.id.webView) // 获取WebView实例
        webView.settings.javaScriptEnabled = true // 启用JavaScript，允许网页中的JavaScript代码执行，同时便于注入脚本
        webView.settings.domStorageEnabled = true  // 启用DOM存储
        webView.settings.loadsImagesAutomatically = true // 自动加载图片

        // 把 “JsBridge” 这个 Kotlin 对象注入到 window.NativeBridge
        webView.addJavascriptInterface(JsBridge(), "NativeBridge")

        val mode = intent.getStringExtra("mode").orEmpty()
        if (mode == "AI_FIRST"){
            AIPlayerNum = 1
        }
        else if (mode == "AI_SECOND"){
            AIPlayerNum = 2
        }

        webClient = GameWebViewClient()
        webView.webViewClient = webClient

        // 加载空盘
        webView.loadUrl(uttStartURL)
    }

    // 让箭头能点
    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            finish()
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    private fun injectHistoryListener(webView: WebView?) {
        // 注入监听 url 变化的代码
        val js = """
            (function(){
                function report(){
                    window.NativeBridge.onUrlChanged(location.href);
                }
                window.addEventListener('popstate', report);
                const originalPush = history.pushState;
                const originalReplace = history.replaceState;
                history.pushState = function() {
                    originalPush.apply(history, arguments);
                    report();
                };
                history.replaceState = function() {
                    originalReplace.apply(history, arguments);
                    report();
                };
                report();
            })();
        """.trimIndent()
        webView?.evaluateJavascript(js, null)
    }
    // 设置AI搜索时的加载界面，同时禁止用户操作以避免异常
    private fun startSearching() {
        // 无需禁用返回键，搜索函数 use 自动管理生命周期

        // 1. 找到 Content
        val content = findViewById<ViewGroup>(android.R.id.content)

        // 2. 构建 mask
        val mask = View(this).apply {
            isClickable = true                    // 必须 true 才能消费事件, 吃掉触摸
            setBackgroundColor(Color.TRANSPARENT) // 全透明
            setOnClickListener {
                Toast.makeText(
                    this@UTTAIvsHuman,   // 换成你的 Activity 类名
                    "AI正在思考中，点击屏幕无效",
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        // 3. 计算 Toolbar 底沿到屏幕顶的距离
        val toolbar = findViewById<Toolbar>(R.id.toolbar)   // 换成你的 id
        val toolbarBottom = toolbar.bottom                  // 如果 Toolbar 在 AppBarLayout 里，用 AppBarLayout 的 bottom 也行

        // 4. 把 mask 加到 Content 的父容器
        val parent = content.parent as ViewGroup
        val index = parent.indexOfChild(content)
        parent.addView(mask, index + 1,                // 插在 content 之上
            FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ).apply {
                topMargin = toolbarBottom             // 关键：让出 Toolbar 区域
            })
        // 把 mask 存到 MASK_TAG，搜完再 removeView
        parent.setTag(R.id.mask_view_tag, mask)

//        // TODO 修改DOM文字，提示 AI 正在搜索
          // 直接执行JS脚本会导致不知道后面的应该显示什么文字
//        webView.evaluateJavascript(
//            """document.querySelector("#root > div > div > div.mx-4.mt-2.flex.items-center.justify-center > span").textContent = 'AI思考中……';""",
//            null
//        )
    }


    private fun searchFinished(newUrl : String) {
        // 显示结果 (直接覆盖加载界面)
        webView.pushSpaRoute(newUrl)

        // 移除 MASK_TAG
        val decor = window.decorView as ViewGroup
        decor.getTag(R.id.mask_view_tag)?.let { mask ->
            (mask as View).also {
                (it.parent as? ViewGroup)?.removeView(it) // 1. 摘掉
            }
            decor.setTag(R.id.mask_view_tag, null)       // 2. 清 tag
        }

    }

    private fun AIDoMove(currentGameUrl: String) = lifecycleScope.launch {
        startSearching()                             // 主线程
        val newUrl = withContext(Dispatchers.IO) {   // 切到 IO 线程
            // 获取 MCTS 参数
            val preferences = PreferenceManager.getDefaultSharedPreferences(this@UTTAIvsHuman)
            getBestMove(currentGameUrl, preferences)  // 耗时调用
        }
        searchFinished(newUrl)
    }

    // 实现在 URL 变化时更新当前玩家
    inner class JsBridge {
        @JavascriptInterface
        fun onUrlChanged(url: String) {
            //  ️该回调在 JS 线程，切回主线程再操作 UI
            Handler(Looper.getMainLooper()).post {
                Log.d("SPA", "React 路由变了 → $url")
                // 重复调用以及异常规则处理
                if (url == currentGameUrl){
                    // 重复调用，不处理
                    return@post
                }
                // 更新 url (即使不合法，防止重复弹窗)
                currentGameUrl = url
                // 异常 url 处理
                if (url.contains("&")) {
                    // 包含 &，说明玩家改了不支持的规则
                    AlertDialog.Builder(this@UTTAIvsHuman)
                        .setTitle("不支持的规则")
                        .setMessage("暂时不支持修改规则，将回到默认规则的空盘，点击确认继续")
                        .setPositiveButton("确认") { _, _ ->
                            webView.pushSpaRoute(uttStartURL)   // 用户点击后再跳转
                        }
                        .setCancelable(false)               // 返回键无效
                        .create()
                        .apply { setCanceledOnTouchOutside(false) } // 点外部无效
                        .show()
                    return@post
                }
                if (uttStartURL !in url){
                    // 玩家进入其他页面，强制回到空盘
                    AlertDialog.Builder(this@UTTAIvsHuman)
                        .setTitle("界面异常")
                        .setMessage("当前界面异常，将回到默认规则的空盘，点击确认继续")
                        .setPositiveButton("确认") { _, _ ->
                            webView.pushSpaRoute(uttStartURL)   // 用户点击后再跳转
                        }
                        .setCancelable(false)               // 返回键无效
                        .create()
                        .apply { setCanceledOnTouchOutside(false) } // 点外部无效
                        .show()
                    return@post
                }


                // 获取当前玩家
                val target = url.substringAfterLast("p=")
                val currentPlayer = target.length / 2 % 2 + 1
                if (currentPlayer == AIPlayerNum){
                    // 轮到AI下棋
                    AIDoMove(url)
                }
            }
        }
    }

    // 自定义 webViewClient
    inner class GameWebViewClient() : WebViewClient() {

        override fun onPageFinished(view: WebView?, url: String?) {
            super.onPageFinished(view, url)
            injectHistoryListener(view)
        }

        override fun onReceivedError(
            view: WebView?,
            errorCode: Int,
            description: String?,
            failingUrl: String?
        ) {
            super.onReceivedError(view, errorCode, description, failingUrl)
            Log.e("WebView", "加载错误 - 错误码: $errorCode, 描述: $description, URL: $failingUrl")
        }
    }
}


// 主动让 SPA 跳转到任意路由，不刷新整页
fun WebView.pushSpaRoute(path: String) {
    evaluateJavascript(
        """
            (function(){
                if (!window.history || !window.history.pushState) return;
                window.history.pushState({}, '', '$path');
                window.dispatchEvent(new PopStateEvent('popstate'));
            })();
        """.trimIndent(),
        null
    )
}




