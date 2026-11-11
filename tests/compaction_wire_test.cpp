// compaction_wire_test - 紧凑 传输 SUBSTITUTION 及其与叉源
// 的相互作用(第2条) cmd_factory: wire_messages_for /
// estimate_wire_tokens).
//
// compaction_threshold_test 覆盖 WHEN
// 紧凑火灾;覆盖 内容 编号 传输 之后看起来像叉子+紧凑的成形:
//
//   1. 原始记录稿没有收缩 − 传输 不变。
//   2. A CompactionRecord 替换 [0], up_to_index 与 一个 合成用户
//      摘要; 末尾 [up_to_index 船是生的 船是没完没了的
//   3. 第传输 ALWAYS号从用户开始(第Anthropic号要求)——
//      合成摘要是一个用户。
//   4. estimate_wire_tokens 计算替代(较小)视图,而不是
//      原始记录- 否则自动 -compaction 将重新 -fire 永远
//   5. 错误记录(up_to_index 0或 > 大小)降解为原始记录
//      而不是破坏记录 传输.
//   6. FORK + COMPACTION 作曲:一个叉子 线程's fork_note (一个真正的用户)
//      船舶在海上航行 传输 未压缩;当一个叉子后来被压缩时,
//      摘要包含 fork_note 前缀和 传输
//      仍然以用户开始。

#include "agtest.hpp"

#include "agentty/runtime/app/cmd_factory.hpp"
#include "agentty/domain/conversation.hpp"

#include <string>

using namespace agentty;


static Message umsg(std::string t) {
    Message m; m.role = Role::User; m.text = std::move(t); return m;
}
static Message amsg(std::string t) {
    Message m; m.role = Role::Assistant; m.text = std::move(t); return m;
}

TEST_CASE("compaction wire") {
    // ── 1:无压缩 + 原始录音稿 ──
    {
        Thread t;
        t.messages = { umsg("q1"), amsg("a1"), umsg("q2"), amsg("a2") };
        auto wire = app::cmd::wire_messages_for(t);
        check(wire.size() == 4, "no compaction: all messages ship");
        check(wire[0].text == "q1" && wire[3].text == "a2",
              "no compaction: verbatim order preserved");
    }

    // ── 2+3:一个CompactionRecord取代前缀 ──
    {
        Thread t;
        t.messages = { umsg("q1"), amsg("a1"), umsg("q2"), amsg("a2"),
                       umsg("q3"), amsg("a3") };
        Thread::CompactionRecord rec;
        rec.up_to_index = 4;                 // 总结 [0]: q1.a2
        rec.summary     = "user asked q1/q2; assistant answered a1/a2";
        t.compactions.push_back(rec);

        auto wire = app::cmd::wire_messages_for(t);
        // 1 汇总+ 末尾(q3, a3)=3
        check(wire.size() == 3, "compaction: prefix collapses to summary + tail");
        check(wire.front().role == Role::User,
              "compaction: wire starts with a User (Anthropic invariant)");
        check(wire.front().is_compact_summary,
              "compaction: head is flagged is_compact_summary");
        check(wire.front().text.find("q1/q2") != std::string::npos,
              "compaction: summary text carried into the head message");
        check(wire[1].text == "q3" && wire[2].text == "a3",
              "compaction: raw tail after the boundary preserved");
    }

    // ── 4: estimate_wire_tokens 给替换视图定价 ──
    {
        Thread t;
        // 一个大前缀,一个紧凑的缩水非常大。
        for (int i = 0; i < 40; ++i)
            t.messages.push_back(i % 2 ? amsg(std::string(4000, 'a'))
                                       : umsg(std::string(4000, 'q')));
        const int raw = app::cmd::estimate_wire_tokens(t);

        Thread::CompactionRecord rec;
        rec.up_to_index = 38;                // 几乎是一切崩溃
        rec.summary     = "short recap";
        t.compactions.push_back(rec);
        const int compacted = app::cmd::estimate_wire_tokens(t);

        check(compacted < raw / 2,
              "estimate: compacted wire is much cheaper than raw "
              "(raw=" + std::to_string(raw) + " compacted="
              + std::to_string(compacted) + ")");
    }

    // ── 5: 记录不正确降解为原始 ──
    {
        Thread t;
        t.messages = { umsg("q1"), amsg("a1") };
        Thread::CompactionRecord bad;
        bad.up_to_index = 99;                // > 大小 – 结构不全
        bad.summary = "ignored";
        t.compactions.push_back(bad);
        auto wire = app::cmd::wire_messages_for(t);
        check(wire.size() == 2 && wire[0].text == "q1",
              "malformed record (index > size): raw transcript sent");

        Thread t2;
        t2.messages = { umsg("q1"), amsg("a1") };
        Thread::CompactionRecord zero;
        zero.up_to_index = 0;                // 0 ~ 无总结
        zero.summary = "ignored";
        t2.compactions.push_back(zero);
        auto wire2 = app::cmd::wire_messages_for(t2);
        check(wire2.size() == 2 && wire2[0].text == "q1",
              "malformed record (index 0): raw transcript sent");
    }

    // ── 6a: fork_note 船上没有船体 传输 ──
    {
        Thread t;
        Message note;
        note.role = Role::User;
        note.fork_note = true;
        note.fork_transcript = "/tmp/parent.transcript.md";
        note.text = "This conversation is a fork; transcript at /tmp/parent…";
        t.messages.push_back(std::move(note));
        t.messages.push_back(umsg("first real question"));

        auto wire = app::cmd::wire_messages_for(t);
        check(wire.size() == 2, "fork: note + prompt both ship (no compaction)");
        check(wire.front().role == Role::User && wire.front().fork_note,
              "fork: the fork_note is the wire head and a User (provider-proof)");
        check(wire[1].text == "first real question",
              "fork: the real prompt follows the note");
    }

    // ── 6b: 将叉子压缩成块 fork_note; 传输 仍然用户 -first ──
    {
        Thread t;
        Message note;
        note.role = Role::User;
        note.fork_note = true;
        note.text = "fork pointer";
        t.messages.push_back(std::move(note));
        t.messages.push_back(umsg("q1"));
        t.messages.push_back(amsg("a1"));
        t.messages.push_back(umsg("q2"));
        t.messages.push_back(amsg("a2"));

        Thread::CompactionRecord rec;
        rec.up_to_index = 3;                 // 包含注释 + q1 + a1
        rec.summary = "forked thread; user asked q1, got a1";
        t.compactions.push_back(rec);

        auto wire = app::cmd::wire_messages_for(t);
        check(wire.front().role == Role::User,
              "fork+compaction: wire still starts with a User");
        check(wire.front().is_compact_summary,
              "fork+compaction: head is the summary (subsumed the fork_note)");
        check(!wire.front().fork_note,
              "fork+compaction: the summary head is NOT flagged fork_note");
        check(wire.back().text == "a2",
              "fork+compaction: raw tail after the boundary preserved");
    }
}

TEST_CASE("compaction reclaim is measured") {
    // 压缩器的全部工作是使下一个请求适合。
    // 没有记录它是否DID。
    //
    // 危险案件并不是一个大叫失败的缩
    // 合物——它几乎完全没有恢复。
    // 接下来的转弯仍然在门槛之上, 紧凑, 不再恢复任何东西,
    // 线程 liverlock 自己总结,
    // 而用户却看它什么都不做。 这只可见于一个 NUMBER ,
    // 这就是为什么 GitHub's 运行时 携带
    // 同样的 pre/post 双人
    //
    // 双方必须来自 相同 计分器或区别是噪音,所以这个针头
    // 是用 cmd::estimate_wire_tokens
    // - 紧凑 -aware 一个
    Thread t;
    t.messages.push_back(umsg(std::string(20000, 'q')));   // 一个大前缀
    t.messages.push_back(amsg(std::string(20000, 'a')));
    t.messages.push_back(umsg("and now a short tail"));

    const int before = app::cmd::estimate_wire_tokens(t);

    Thread::CompactionRecord rec;
    rec.up_to_index   = 2;              // 包含两个大消息
    rec.summary       = "user asked a long thing; assistant answered";
    rec.tokens_before = before;
    t.compactions.push_back(rec);
    t.compactions.back().tokens_after = app::cmd::estimate_wire_tokens(t);

    const auto& r = t.compactions.back();

    // 测量是真实的:一个大前缀被一个简短的总结所取代,
    // 必须大大缩小传输.
    check(r.tokens_after < r.tokens_before,
          "compaction reclaim: after is smaller than before");
    check(r.reclaimed().has_value(),
          "compaction reclaim: a measured record reports a reclaim");
    check(*r.reclaimed() > 1000,
          "compaction reclaim: subsuming ~40KB reclaims real tokens");

    // 也同意汽车的预估 -compaction 触发读,
    // 这是唯一的原因 before/after 完全可以比较。
    check(r.tokens_after == app::cmd::estimate_wire_tokens(t),
          "compaction reclaim: after matches the live wire estimate");
}

TEST_CASE("an unmeasured compaction reports unknown, not zero") {
    // 测量方法存在前用0/0.重新装入的记录
    // 零开垦和 "we never measured"
    // 想要( E) OPPOSITE 反应——第一个
    // 表示收缩被打破, 线程 即将出现,第二,我们根本不知道,
    // 把他们吓得一团糟 线程 报告从未发生过的灾难。
    Thread t;
    t.messages.push_back(umsg("q"));
    t.messages.push_back(amsg("a"));

    Thread::CompactionRecord rec;
    rec.up_to_index = 1;
    rec.summary     = "old record, written before tokens were tracked";
    t.compactions.push_back(rec);

    check(!t.compactions.back().reclaimed().has_value(),
          "unmeasured compaction reports nullopt rather than 0");

    // 半-measured记录也是未知的:单
    // 靠一方无法产生差异,而另一方猜测是数据发明。
    t.compactions.back().tokens_before = 5000;
    check(!t.compactions.back().reclaimed().has_value(),
          "half-measured compaction is still unknown");
}

TEST_CASE("soft trim keeps the live request") {
    // ~ 1 143个令牌/每4000-byte 条消息,3.5bytes/token.
    const std::string big(4000, 'x');

    // 长工具环:q1,然后q2,之后是许多助手sub-turns.
    // 旧前线 -trim 剥掉q2,只留下最后一个助手.
    {
        std::vector<Message> v = { umsg("q1"), amsg(big), umsg("q2") };
        for (int i = 0; i < 10; ++i) v.push_back(amsg(big));
        app::cmd::soft_trim_to_ceiling(v, 3000);
        bool has_q2 = false;
        for (auto& m : v) if (m.text == "q2") has_q2 = true;
        check(has_q2, "latest user message survives the trim");
        check(v.front().role == Role::User, "wire still opens with a User");
        check(v.back().text == big, "newest message survives");
    }

    // 第一次大战 提示词 单是溢出:头去,活转而留.
    {
        std::vector<Message> v = { umsg(std::string(40000, 'h')), amsg("a1"),
                                   umsg("q2"), amsg("a2") };
        app::cmd::soft_trim_to_ceiling(v, 1000);
        check(v.front().text == "q2", "oversized head is dropped last");
        check(v.size() == 2, "live turn kept whole");
    }

    // 已经适应了:没有变化。
    {
        std::vector<Message> v = { umsg("q1"), amsg("a1"), umsg("q2") };
        app::cmd::soft_trim_to_ceiling(v, 100000);
        check(v.size() == 3, "under the ceiling is a no-op");
    }

    // 头IS 直播请求:从未放弃.
    {
        std::vector<Message> v = { umsg(std::string(40000, 'h')), amsg(big) };
        app::cmd::soft_trim_to_ceiling(v, 100);
        check(!v.empty() && v.front().role == Role::User,
              "sole user message is never dropped");
    }
}
