// 用于人机对战的高级接口
package com.aiprojects.gameai

import android.content.SharedPreferences
import android.util.Log
import java.io.Closeable


// 实现 RAII, 把裸指针对装成可自动关闭的资源
// 同时增加基于url的操作
class GamePtr private constructor(
    private var ptr: Long,
    private var gameUrl: String = "https://game.hullqin.cn/jzq?p="
) : Closeable {

    companion object {
        // 初始化函数（空盘）
        fun create(): GamePtr{
            return GamePtr(NativeUTT.nCreateEmpty())
        }
    }

    /** 只读访问，不允许外部改值 */
    fun get() = ptr
    fun makeMove(action: String){
        val col = action[0] - 'a'
        val row = 8 - (action[1] - '1')
        // 更新gameUrl
        NativeUTT.nMakeMove(ptr, row, col)
        gameUrl = gameUrl + action
    }

    // 获取当前游戏状态的 url
    fun getUrl(): String = gameUrl

    // 检查当前游戏是否结束：0未结束，1玩家X获胜，2玩家O获胜，-1平局
    fun getDoneWinner(): Int = NativeUTT.nGetDoneWiner(ptr)

    // 获取当前玩家
    fun getCurrentPlayer(): Int = NativeUTT.nGetCurrentPlayer( ptr)

    // 把棋盘同步到 newUrl 描述的状态
    fun updateBoard(newUrl: String){
        // 如果传入的 url 和当前游戏状态 url 相同，则不更新（一次走子，onUrlChanged会被重复调用）
        if (newUrl == gameUrl) return
        // 传入 url 不合法会在 activity 层处理

        val target = newUrl.substringAfterLast("p=")
        // 从空盘开始一次性重放
        // 这里必须用 native 指针手动管理内存
        val newPtr = NativeUTT.nCreateEmpty()
        var step = 0
        while (step < target.length) {
            // 每步固定 2 字符：字母+数字
            val action = target.substring(step, step + 2)
            val col = action[0] - 'a'
            val row = 8 - (action[1] - '1')
            NativeUTT.nMakeMove(newPtr, row, col)
            step += 2
        }
        NativeUTT.nDestroy(ptr)   // 先释放旧内存
        ptr = newPtr                // 再接管新内存
        gameUrl = newUrl            // 同步记录
    }

    override fun close() {
        val p = ptr
        if (p != 0L) {
            NativeUTT.nDestroy(p)
            ptr = 0L          // 防二次释放
        }
    }
}


/* 对 MCTS 同理 */
class MCTSPurePtr private constructor(
    private var ptr: Long
) : Closeable {

    companion object {
        fun create(nPlayout: Int, cPuct: Float): MCTSPurePtr =
            MCTSPurePtr(NativeMCTSPure.nCreate(nPlayout, cPuct))
    }

    fun get() = ptr

    // 获取最佳动作
    // 返回 (row, col)
    fun getBestMove(gamePtr: GamePtr): Pair<Int,Int> {
        val actionIndex = NativeMCTSPure.nGetBestMoveIndex(ptr, gamePtr.get())
        val row = actionIndex / 9
        val col = actionIndex % 9
        return Pair(row, col)
    }

    // 获取最佳动作对应的 url String 格式
    fun getBestMoveStr(gamePtr: GamePtr): String {
        val (row, col) = getBestMove(gamePtr)
        // row_str: 0~8 -> 9~1
        val row_str = (8 - row + 1).toString()
        // col_str: 0~8 -> a~i
        val col_str = (col + 'a'.code).toChar().toString()
        return col_str + row_str
    }

    override fun close() {
        val p = ptr
        if (p != 0L) {
            NativeMCTSPure.nDestroy(p)
            ptr = 0L
        }
    }
}

// 根据url获取最佳动作
// 这里实时构造对象，开销较小，避免内存泄漏的同时也可以实现MCTS参数的动态调整
fun getBestMoveURL(gameUrl: String, preferences: SharedPreferences) : String{
    // 双层 use 确保资源被释放
    // TODO 用户退出时，会等运算完成后资源被释放
    return GamePtr.create().use { gamePtr ->
        // 同步棋盘
        gamePtr.updateBoard(gameUrl)
        // 根据 preferences，构造 MCTSPurePtr
        val nPlayout = preferences.getString("mcts_pure_n_playout", "2000")?.toIntOrNull() ?: 2000
        val cPuct = preferences.getString("mcts_pure_c_puct", "0.8")?.toFloatOrNull() ?: 0.8f
        MCTSPurePtr.create(nPlayout, cPuct).use { mctsPtr ->
            // MCTS 搜索
            val bestMove = mctsPtr.getBestMoveStr(gamePtr)
            // 更新 gamePtr
            gamePtr.makeMove(bestMove)
            // 获取当前游戏状态的 url
            gamePtr.getUrl() // 返回当前游戏状态的 url
        }
    }
}

// 根据url获取游戏是否结束
fun getDoneWinnerUrl(gameUrl: String): Int {
    return GamePtr.create().use { gamePtr ->
        gamePtr.updateBoard(gameUrl)
        gamePtr.getDoneWinner()
    }
}

// 根据url返回piece id 和当前玩家 id
fun getBestMoveIDPlayer(gameUrl: String, preferences: SharedPreferences) : Pair<Int, Int>{
    // 双层 use 确保资源被释放
    // TODO 用户退出时，会等运算完成后资源被释放
    return GamePtr.create().use { gamePtr ->
        // 同步棋盘
        gamePtr.updateBoard(gameUrl)
        // 根据 preferences，构造 MCTSPurePtr
        val nPlayout = preferences.getString("mcts_pure_n_playout", "2000")?.toIntOrNull() ?: 2000
        val cPuct = preferences.getString("mcts_pure_c_puct", "0.8")?.toFloatOrNull() ?: 0.8f
        MCTSPurePtr.create(nPlayout, cPuct).use { mctsPtr ->
            // MCTS 搜索
            val bestMove = mctsPtr.getBestMove(gamePtr)
            // 返回最佳动作对应的 piece id 和当前玩家 id
            Pair (((8 - bestMove.first) * 9 + bestMove.second), gamePtr.getCurrentPlayer())
        }
    }
}
