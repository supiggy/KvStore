#!/usr/bin/env python3
# ============================================================================
# embed.py —— MiniVec 的 embedding 客户端
#   把"文本"变成"向量",通过 TCP 文本协议灌进 MiniVec server,并能语义查询。
#
# 用法:
#   1) 灌库:   python3 embed.py index corpus.txt
#   2) 查询:   python3 embed.py query "怎么训练神经网络" 3
#
# 两种 embedding 后端:
#   - 默认:sentence-transformers 的 all-MiniLM-L6-v2(384 维,真·语义)
#           需要: pip install sentence-transformers
#   - --fake:纯词袋哈希(384 维,只有"词面"相似,无语义)。
#           不依赖任何 ML 库,用来验证"客户端<->server"管线能不能跑通。
#
# 注意:
#   * MiniVec 只存 id+向量,不存原文 → 本脚本把 id->原文 存到 corpus.txt.map.json
#   * server 维度必须 = 384(minivec.h 里 MINIVEC_DIM);换模型要同步改并重新 make
#   * 协议是"一问一答":发一条命令、读一行响应、再发下一条(避免粘包)
# ============================================================================
import sys, socket, json, hashlib, math, os

HOST, PORT, DIM = "127.0.0.1", 9097, 384


# ---------- embedding 后端 ----------
def embed_fake(text: str):
    """词袋哈希:每个词 hash 到一个维度上 +1,再归一化。
    共享词多的文本向量更接近 —— 只够演示管线,不是语义。"""
    v = [0.0] * DIM
    for tok in text.lower().split():
        h = int(hashlib.md5(tok.encode()).hexdigest(), 16)
        v[h % DIM] += 1.0
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / n for x in v]


_model = None
def embed_real(text: str):
    """sentence-transformers all-MiniLM-L6-v2 → 384 维语义向量。"""
    global _model
    if _model is None:
        from sentence_transformers import SentenceTransformer
        _model = SentenceTransformer("all-MiniLM-L6-v2")
    return _model.encode(text, normalize_embeddings=True).tolist()


def embed(text, fake):
    return embed_fake(text) if fake else embed_real(text)


# ---------- 与 server 通信(一问一答) ----------
def send_cmd(sock, line: str) -> str:
    sock.sendall((line + "\n").encode())
    data = sock.recv(65536)          # 响应都很小,一次读够
    return data.decode(errors="replace").strip()


def vadd_line(idx, vec):
    return "VADD %d %s" % (idx, " ".join("%.6f" % x for x in vec))


def vsearch_line(topk, vec):
    return "VSEARCH %d %s" % (topk, " ".join("%.6f" % x for x in vec))


# ---------- 子命令 ----------
def cmd_index(corpus_path, fake):
    with open(corpus_path, encoding="utf-8") as fp:
        docs = [ln.strip() for ln in fp if ln.strip()]
    id_map = {}
    with socket.create_connection((HOST, PORT)) as sock:
        for i, doc in enumerate(docs, start=1):
            resp = send_cmd(sock, vadd_line(i, embed(doc, fake)))
            id_map[i] = doc
            print("VADD %3d  %-30.30s -> %s" % (i, doc, resp))
    with open(corpus_path + ".map.json", "w", encoding="utf-8") as fp:
        json.dump(id_map, fp, ensure_ascii=False)
    print("已灌入 %d 条,id->原文 映射存到 %s.map.json" % (len(docs), corpus_path))


def cmd_query(text, topk, fake, map_path):
    id_map = {}
    if map_path and os.path.exists(map_path):
        id_map = json.load(open(map_path, encoding="utf-8"))
    with socket.create_connection((HOST, PORT)) as sock:
        resp = send_cmd(sock, vsearch_line(topk, embed(text, fake)))
    print("查询: %s\n--- top%d ---" % (text, topk))
    for ln in resp.splitlines():
        parts = ln.split()
        if len(parts) == 2:
            vid, score = parts
            print("  id=%s  score=%s  %s" % (vid, score, id_map.get(vid, "")))
        else:
            print("  " + ln)


def main():
    args = [a for a in sys.argv[1:] if a != "--fake"]
    fake = "--fake" in sys.argv
    if not args:
        print(__doc__ if __doc__ else "用法: embed.py index <corpus> | query <text> [topk]")
        return
    if args[0] == "index" and len(args) >= 2:
        cmd_index(args[1], fake)
    elif args[0] == "query" and len(args) >= 2:
        topk = int(args[2]) if len(args) >= 3 else 3
        cmd_query(args[1], topk, fake, "corpus.txt.map.json")
    else:
        print("用法: embed.py index <corpus.txt> [--fake] | query <text> [topk] [--fake]")


if __name__ == "__main__":
    main()
