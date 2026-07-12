/* ============================================================================
 * LeetCode 743. Network Delay Time —— Dijkstra(优先队列版,C++)
 * ----------------------------------------------------------------------------
 * 编译运行: g++ -std=c++17 -Wall leetcode-743-dijkstra.cpp -o dij && ./dij
 *
 * 为什么放这:HNSW 的 search_layer 本质就是"用堆做最佳优先图搜索",
 *   和 Dijkstra 同构 —— 都是"小顶堆里取最近的点 → 扩展它的邻居 → 更新候选"。
 *   看懂 Dijkstra 的优先队列循环,就看懂了 search_layer 的骨架。
 *
 * 对应关系:
 *   Dijkstra                         HNSW search_layer
 *   --------------------------------------------------------------
 *   priority_queue(小顶堆,按距离)   candidates(小顶堆,按到 query 距离)
 *   取堆顶最近点 u                    取最近的待扩展候选 c
 *   遍历 u 的邻居 v,松弛 dist[v]      遍历 c 的邻居 nb,算 dist 入候选
 *   dist[] 记录最短距离               visit_tag[] 记录已访问 + W 保留最优 ef 个
 *
 * 题目:n 个点,有向带权边 times[i]=(u,v,w),从 k 出发,
 *       返回信号到达所有点的最短用时的最大值;到不全返回 -1。
 * ============================================================================ */
#include <iostream>
#include <vector>
#include <queue>
#include <climits>
#include <algorithm>
#include <utility>
using namespace std;

int networkDelayTime(vector<vector<int>>& times, int n, int k) {
    /* 1. 建邻接表: g[u] = { (v, w), ... } */
    vector<vector<pair<int,int>>> g(n + 1);
    for (auto& e : times) g[e[0]].push_back({e[1], e[2]});

    /* 2. dist[i] = 从 k 到 i 的当前最短距离,初始无穷 */
    vector<int> dist(n + 1, INT_MAX);
    dist[k] = 0;

    /* 3. 小顶堆:(距离, 节点),距离小的先出。
     *    —— 和 HNSW candidates 一样,永远先扩展"目前最近"的点(贪心方向)。 */
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<pair<int,int>>> pq;
    pq.push({0, k});

    /* 4. 主循环:取最近点 → 扩展邻居 → 松弛 */
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;              /* 过期项(已有更短路径)→ 跳过 */
        for (auto& [v, w] : g[u]) {             /* 扩展 u 的邻居(像 search_layer 扩展邻居)*/
            if (dist[u] + w < dist[v]) {        /* 松弛:经 u 到 v 更短 */
                dist[v] = dist[u] + w;
                pq.push({dist[v], v});
            }
        }
    }

    /* 5. 所有点的最短距离里取最大;有点到不了 → -1 */
    int ans = 0;
    for (int i = 1; i <= n; i++) {
        if (dist[i] == INT_MAX) return -1;
        ans = max(ans, dist[i]);
    }
    return ans;
}

int main() {
    vector<vector<int>> times = {{2,1,1}, {2,3,1}, {3,4,1}};
    cout << "networkDelayTime = " << networkDelayTime(times, 4, 2)
         << "  (expect 2)\n";

    vector<vector<int>> t2 = {{1,2,1}};
    cout << "unreachable case  = " << networkDelayTime(t2, 2, 2)
         << "  (expect -1, 从2到不了1)\n";
    return 0;
}
