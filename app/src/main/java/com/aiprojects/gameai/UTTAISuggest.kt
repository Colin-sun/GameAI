package com.aiprojects.gameai

import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.webkit.JavascriptInterface
import kotlin.coroutines.resume
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.Toolbar
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.DrawableCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.lifecycleScope
import androidx.preference.PreferenceManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext

private const val startURL = "https://game.hullqin.cn/jzq"
private const val uttGameStartURL = "https://game.hullqin.cn/jzq?p=" // 空盘的URL

class UTTAISuggest : AppCompatActivity() {
    private lateinit var webView: WebView
    private var currentUrl = "" // 当前URL
    private var currentText = "" // 当前文字
    private var currentRule = "" // 当前规则
    private lateinit var webClient: GameWebViewClient
    private lateinit var toolbar : Toolbar // 全局化 Toolbar 实例，便于之后多处使用

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContentView(R.layout.activity_uttaisuggest)
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main)) { v, insets ->
            val systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars())
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom)
            insets
        }
        toolbar = findViewById(R.id.toolbar)

        // 显示返回箭头（←）
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        // 把箭头刷成白色
        val whiteArrow = ContextCompat.getDrawable(this, R.drawable.baseline_arrow_back_24)
        whiteArrow?.let {
            DrawableCompat.setTint(it, Color.WHITE)
        }
        supportActionBar?.setHomeAsUpIndicator(whiteArrow)

        webView = findViewById(R.id.webView) // 获取WebView实例
        webView.settings.javaScriptEnabled = true // 启用JavaScript，允许网页中的JavaScript代码执行，同时便于注入脚本
        webView.settings.domStorageEnabled = true  // 启用DOM存储
        webView.settings.loadsImagesAutomatically = true // 自动加载图片

        // 注册两个 JS 接口，把 “JsBridge” 这个 Kotlin 对象注入到 window.NativeBridge
        webView.addJavascriptInterface(JsBridge(), "NativeBridge")

        webClient = GameWebViewClient()
        webView.webViewClient = webClient

        // 加载进入房间的页面
        // 修改 toolbar 标题，提示用户进入对局
        toolbar.title = "等待进入对局..."
        webView.loadUrl(startURL)
    }

    // 让箭头能点
    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            finish()
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    // 注入监听 url 变化的代码
    private fun injectHistoryListener(webView: WebView?) {
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

    // 注入 JavaScript 监听文字变化
    private fun injectTextListener(webView: WebView?) {
        val js = """
        (function() {
            const selector = '#root > div > div > div.mt-2 > div.space-x-4.flex.items-center.justify-center > span';
            let target = null;   // 当前元素
            let isExist = false; // 元素是否存在
            
            /* ---------- 元素就绪：首次回调 + 建立两条监听 ---------- */
            function onTargetReady(el) {
                target   = el;
                isExist = true; // 元素已存在
            
                window.NativeBridge.onTextChanged(target.textContent); // 首次回调，发送文字变化
            
                // 监听文字变化 
                textObserver = new MutationObserver(() => {
                    window.NativeBridge.onTextChanged(target.textContent);
                });
                textObserver.observe(target, {
                    childList: true,
                    characterData: true,
                    subtree: true
                });
                
                // 监听元素本身被移除 
                deleteObserver = new MutationObserver(() => {
                    if (!document.contains(target)) {
                        isExist = false; // 元素将被移除
                        textObserver.disconnect();
                        deleteObserver.disconnect();
                        window.NativeBridge.onTextChanged("");
                    }
                });
                deleteObserver.observe(document.body, { childList: true, subtree: true });
            }
            // 每 100ms 检查一次元素是否出现
            const interval = setInterval(() => {
                if (isExist) return;
                const el = document.querySelector(selector);
                if (el) {
                    onTargetReady(el);
                }
            }, 100);
        })();
        """.trimIndent()
        webView?.evaluateJavascript(js, null)
    }

    // 监听规则文字变化
    private fun injectRuleListener(webView: WebView?) {
        val js = """
        (function() {
            const rule_selector = "#root > div > div > div.mt-2 > div.mx-4.mt-4.flex.items-center.justify-center";
            // 同时监听有没有更改的权限
            let rule_target = null;   // 当前元素
            let rule_isExist = false; // 元素是否存在
            
            /* ---------- 元素就绪：首次回调 + 建立两条监听 ---------- */
            function onRuleTargetReady(el) {
                rule_target   = el;
                rule_isExist = true; // 元素已存在
            
                window.NativeBridge.onRuleChanged(rule_target.textContent); // 首次回调，发送文字变化
            
                // 监听文字变化 
                rule_textObserver = new MutationObserver(() => {
                    window.NativeBridge.onRuleChanged(rule_target.textContent);
                });
                rule_textObserver.observe(rule_target, {
                    childList: true,
                    characterData: true,
                    subtree: true
                });
                
                // 监听元素本身被移除 
                rule_deleteObserver = new MutationObserver(() => {
                    if (!document.contains(rule_target)) {
                        rule_isExist = false; // 元素将被移除
                        rule_textObserver.disconnect();
                        rule_deleteObserver.disconnect();
                        window.NativeBridge.onRuleChanged("");
                    }
                });
                rule_deleteObserver.observe(document.body, { childList: true, subtree: true });
            }
            // 每 100ms 检查一次元素是否出现
            const rule_interval = setInterval(() => {
                if (rule_isExist) return;
                const el = document.querySelector(rule_selector);
                if (el) {
                    onRuleTargetReady(el);
                }
            }, 100);
        })();
        """.trimIndent()
        webView?.evaluateJavascript(js, null)
    }

    // AI 建议显示函数
    private fun ingectSuggestShow(webView: WebView?){
        val js = """
            function showSuggest(piece_id, player_id) {
                let newstyle;
                if (player_id === 1) {
                    const old = document.querySelector('#black');
                    newstyle = old.cloneNode(true);
                    newstyle.id = 'suggest' + player_id.toString();
                    newstyle.querySelector('circle').setAttribute('stroke', '#6200EE');
                } else {
                    const old = document.querySelector('#white');
                    newstyle = old.cloneNode(true);
                    newstyle.id = 'suggest' + player_id.toString();
                    newstyle.querySelector('path').setAttribute('stroke', '#6200EE');
                }
                document.querySelector('#svg defs').appendChild(newstyle);

                const useEl = document.getElementById('piece-' + piece_id.toString());
                if (useEl) {
                    useEl.setAttributeNS('http://www.w3.org/1999/xlink', 'xlink:href', '#suggest' + player_id.toString());
                    useEl.style.opacity = '0.5';
                }
                return true;
            }
        """.trimIndent()
        webView?.evaluateJavascript(js, null)
    }


    // 设置AI搜索时的加载界面，同时禁止用户操作以避免异常
    private fun startSearching(){
        // 无需禁用返回键，搜索函数 use 自动管理生命周期

        // 1. 找到 Content
        val content = findViewById<ViewGroup>(android.R.id.content)

        // 2. 构建 mask
        val mask = View(this).apply {
            isClickable = true                    // 必须 true 才能消费事件, 吃掉触摸
            setBackgroundColor(Color.TRANSPARENT) // 全透明
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

        // 修改 Toolbar标题，提示 AI 正在搜索
        toolbar.title = "AI思考中，点击屏幕无效"
    }

    // 返回当前棋盘的 url 表示
    // 由于 evaluateJavascript 异步处理，所以需要单开 suspendCancellableCoroutine
    private suspend fun getBoardUrl(): String =
        suspendCancellableCoroutine { cont ->
            webView.evaluateJavascript("""
            (function(){
                return Array.from(document.querySelectorAll('use'))
                    .filter(el => !(el.hasAttribute('id') && el.classList.contains('jzq-undropped-piece')))
                    .map(el => {
                        const x = parseFloat(el.getAttribute('x'));
                        const y = parseFloat(el.getAttribute('y'));
                        const col = String.fromCharCode(97 + (x + 40) / 10);
                        const row = String.fromCharCode(49 + (40 - y) / 10);
                        return col + row;
                    })
                    .join('');
            })();
            """.trimIndent()
            )  { result ->
                cont.resume( uttGameStartURL + result?.replace("\"", ""))
            }
        }


    private fun searchFinished(piece_player : Pair<Int, Int>) {
        // 延迟恢复标题，避免闪
        Handler(Looper.getMainLooper()).postDelayed({
            val toolbar: Toolbar = findViewById(R.id.toolbar)
            toolbar.title = "辅助走子"
            // 显示结果
            webView.evaluateJavascript(
                "showSuggest(${piece_player.first}, ${piece_player.second})",
                null
            )
            // 移除 MASK_TAG
            val decor = window.decorView as ViewGroup
            decor.getTag(R.id.mask_view_tag)?.let { mask ->
                (mask as View).also {
                    (it.parent as? ViewGroup)?.removeView(it) // 1. 摘掉
                }
                decor.setTag(R.id.mask_view_tag, null)       // 2. 清 tag
            }
        }, 350)
    }

    private fun getAISuggest() = lifecycleScope.launch {
        startSearching()                       // 主线程
        val currentGameUrl = getBoardUrl()
        val piece_player = withContext(Dispatchers.IO) {   // 切到 IO 线程
            // 获取 MCTS 参数
            val preferences = PreferenceManager.getDefaultSharedPreferences(this@UTTAISuggest)
            getBestMoveIDPlayer(currentGameUrl, preferences)  // 耗时调用
        }
        searchFinished(piece_player)
    }

    // 监听指示游戏状态的 text 从而判断是否需要 AI 建议
    inner class JsBridge {
        @JavascriptInterface
        fun onUrlChanged(url: String) {
            //  ️该回调在 JS 线程，切回主线程再操作 UI
            Handler(Looper.getMainLooper()).post {
                // 重复调用以及异常规则处理
                if (url == currentUrl){
                    // 重复调用，不处理
                    return@post
                }
                // 更新 url (即使不合法，防止重复弹窗)
                currentUrl = url
                // 异常 url 处理
                if (startURL !in url){
                    // 玩家进入其他页面，强制回到主界面
                    AlertDialog.Builder(this@UTTAISuggest)
                        .setTitle("界面异常")
                        .setMessage("当前界面异常，将回到游戏主页，点击确认继续")
                        .setPositiveButton("确认") { _, _ ->
                            webView.pushSpaRoute(startURL)   // 用户点击后再跳转
                        }
                        .setCancelable(false)               // 返回键无效
                        .create()
                        .apply { setCanceledOnTouchOutside(false) } // 点外部无效
                        .show()
                    return@post
                }
                // 其他所有逻辑都在 onTextChanged 处理
            }
        }

        @JavascriptInterface
        fun onTextChanged(newText: String) {
            Handler(Looper.getMainLooper()).post {
                // 重复调用，不处理
                if (newText == currentText){
                    return@post
                }
                val oldText = currentText
                currentText = newText
                // 人性化提示回到对局
                if (newText.isEmpty() && oldText.isNotEmpty()){
                    toolbar.title = "等待回到对局..."
                    return@post
                }
                // 提示等待轮到本方下棋
                if (newText != "等你下棋"){
                    toolbar.title = "等待轮到本方下棋..."
                    return@post
                }
                // 轮到本方下棋，获取 AI 建议
                getAISuggest()

            }
        }

        // 处理不合法规则
        @JavascriptInterface
        fun onRuleChanged(newRule: String){
            Handler(Looper.getMainLooper()).post {
                // 重复调用，不处理
                if (newRule == currentRule){
                    return@post
                }
                currentRule = newRule
                // 合法规则，不处理
                if (newRule == "" || "禁止落子" in newRule){
                    return@post
                }
                // 可以修改的不合法规则，弹窗提示
                if ("切换模式" in newRule){
                    AlertDialog.Builder(this@UTTAISuggest)
                        .setTitle("规则不合法")
                        .setMessage("当前房间规则不合法，点击确认自动修改")
                        .setPositiveButton("确认") { _, _ ->
                            webView.evaluateJavascript("""
                                document.querySelector("#root > div > div > div.mt-2 > div.mx-4.mt-4.flex.items-center.justify-center > button")
                                .click()
                            """.trimIndent(),null)
                        }
                        .setCancelable(false)
                        .create()
                        .apply { setCanceledOnTouchOutside(false) }
                        .show()
                    return@post
                } else{
                    // 无法修改的不合法规则，弹窗提示
                    AlertDialog.Builder(this@UTTAISuggest)
                        .setTitle("规则不合法")
                        .setMessage("当前房间规则不合法，点击确认回到主页")
                        .setPositiveButton("确认") { _, _ ->
                            // 弹窗确认，回到主页
                            webView.pushSpaRoute(startURL)
                        }
                        .setCancelable(false)
                        .create()
                        .apply { setCanceledOnTouchOutside(false)}
                        .show()
                    return@post
                }
            }
        }
    }

    // 自定义 webViewClient
    inner class GameWebViewClient() : WebViewClient() {

        override fun onPageFinished(view: WebView?, url: String?) {
            super.onPageFinished(view, url)
            // 注入三个监听器，一个 AI 建议显示函数
            injectHistoryListener(view)
            injectTextListener(view)
            ingectSuggestShow( view)
            injectRuleListener( view)
        }

        override fun onReceivedError(
            view: WebView?,
            request: android.webkit.WebResourceRequest?,
            error: android.webkit.WebResourceError?
        ) {
            super.onReceivedError(view, request, error)
            Log.e("WebView", "加载错误 - 错误码: ${error?.errorCode}, 描述: ${error?.description}, URL: ${request?.url}")
        }

    }
}

// 主动让 SPA 跳转到任意路由，不刷新整页
private fun WebView.pushSpaRoute(path: String) {
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
