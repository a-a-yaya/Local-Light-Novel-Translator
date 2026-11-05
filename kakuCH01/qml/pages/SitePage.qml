import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KakuCH

// 「网站」页：左侧上方可在「排行」「检索」间切换；中间结果与翻页；右侧详情。
// 分类筛选与搜索框文本分开；推荐关键词仍直接写入搜索框。
Page {
    id: page
    objectName: "site"
    background: Rectangle { color: Theme.pageBackground }

    property var genres: controller.siteGenres()
    property var periods: controller.sitePeriods()
    property var lengths: controller.siteLengths()
    property var searchGenres: [
        { jp: "異世界ファンタジー", zh: "异世界奇幻" },
        { jp: "現代ファンタジー", zh: "现代奇幻" },
        { jp: "SF", zh: "科幻" },
        { jp: "恋愛", zh: "恋爱" },
        { jp: "ラブコメ", zh: "恋爱喜剧" },
        { jp: "現代ドラマ", zh: "现代剧情" },
        { jp: "ホラー", zh: "恐怖" },
        { jp: "ミステリー", zh: "悬疑" },
        { jp: "エッセイ・ノンフィクション", zh: "随笔·纪实" },
        { jp: "歴史・時代・伝奇", zh: "历史·时代·传奇" },
        { jp: "創作論・評論", zh: "创作论·评论" },
        { jp: "詩・童話・その他", zh: "诗歌·童话·其他" },
        { jp: "魔法のiらんど", zh: "魔法i乐园" },
        { jp: "二次創作", zh: "二次创作" }
    ]
    property string curGenre: "all"
    property string curPeriod: "weekly"
    property int detailTab: 0
    // A method result is a value snapshot. Indexing a C++ sequence property can
    // re-run its filtered getter for every element.
    property var cardSnapshot: controller.siteCardsSnapshot()
    readonly property var filteredCardSnapshot: filteredCards()
    Connections {
        target: controller
        function onSiteCardsChanged() { page.cardSnapshot = controller.siteCardsSnapshot() }
    }
    property var starOptions: [
        { v: 0, n: "不限" }, { v: 1000, n: "1000+" },
        { v: 5000, n: "5000+" }, { v: 10000, n: "10000+" }
    ]
    property var statusOptions: [
        { v: 0, n: "全部" }, { v: 1, n: "连载中" }, { v: 2, n: "已完结" }
    ]
    property bool inSearchMode: controller.siteMode === "search"
    onInSearchModeChanged: page.openGroup = ""
    property bool worksSearch: controller.siteSearchKind === "works"
    onWorksSearchChanged: page.openGroup = ""
    property var sortOptions: [
        { value: "popular", name: "累计排行" },
        { value: "weekly_ranking", name: "周排行" },
        { value: "published_at", name: "新作优先" },
        { value: "last_episode_published_at", name: "更新优先" }
    ]
    // 关键词分类面板（对应站点「おすすめキーワード」的角色 / 故事 / 其他）
    property string openGroup: ""
    property var condChecked: ({})
    // 两条「条件筛选」：exclude = 勾选后排除含该内容的作品；include = 勾选后只保留含该内容的作品。
    // 标记来自页面内嵌的 __NEXT_DATA__（站点不做服务端条件过滤，URL 参数会被忽略），
    // 所以这里是本地筛选，只作用于当前结果页。
    // openGroup 用下面的 ASCII id，避免中文字面量比较出错。
    property var condGroups: [
        { id: "include", label: "只保留含有" },
        { id: "exclude", label: "排除含有" }
    ]
    property var condOpts: [
        { key: "isCruel", zh: "残酷描写", jp: "残酷描写有り" },
        { key: "isViolent", zh: "暴力描写", jp: "暴力描写有り" },
        { key: "isSexual", zh: "性描写", jp: "性描写有り" },
        { key: "isNext", zh: "NEXT 刊登小说", jp: "ネクスト掲載小説" },
        { key: "hasPublication", zh: "已出版 / 多媒体化", jp: "書籍化・メディアミックスした小説" }
    ]
    function isCondGroup(g) {
        return g === "include" || g === "exclude"
    }
    function groupLabel(g) {
        for (var i = 0; i < page.condGroups.length; ++i) {
            if (page.condGroups[i].id === g)
                return page.condGroups[i].label
        }
        return g
    }
    function condHint(g) {
        return g === "exclude"
               ? "勾选后，把含该内容的作品从结果中排除（本地筛选，只作用于当前结果页）"
               : "勾选后，只保留含有该内容的作品（本地筛选，只作用于当前结果页）"
    }
    function condOn(k) {
        return page.condChecked[page.openGroup + "|" + k] === true
    }
    function toggleCond(k) {
        var m = page.condChecked
        var key = page.openGroup + "|" + k
        if (m[key] === true)
            delete m[key]
        else
            m[key] = true
        page.condChecked = m
    }
    // 列表为空时该显示什么：优先显示失败/无结果信息，其次是本地筛选说明，最后是引导语
    function emptyHint() {
        if (!controller.siteBusy && page.cardSnapshot.length === 0
            && controller.siteMessage.indexOf("共 ") === 0)
            return "本页没有符合筛选条件的作品，请调整条件或尝试翻页"
        if (page.cardSnapshot.length === 0 && !controller.siteBusy
            && controller.siteMessage.length > 0)
            return controller.siteMessage
        if (page.condActive() && page.cardSnapshot.length > 0)
            return "当前结果页没有符合已勾选条件的作品（本地筛选只作用于当前结果页）"
        return page.inSearchMode
                ? (page.worksSearch ? "输入关键词或勾选分类后点「检索」"
                                    : "输入作者 ID 或笔名后点「检索」")
                : "左侧选择类型 / 周期"
    }
    function condActive() {
        if (!page.inSearchMode) return false
        var m = page.condChecked
        for (var k in m) {
            if (m[k] === true)
                return true
        }
        return false
    }
    // 两条「条件筛选」的本地结果
    function filteredCards() {
        var cards = page.cardSnapshot
        if (!page.inSearchMode) return cards
        var out = []
        for (var i = 0; i < cards.length; ++i) {
            var c = cards[i]
            if (c.flagsKnown !== true) {
                out.push(c)
                continue
            }
            var keep = true
            for (var e = 0; e < page.condOpts.length; ++e) {
                var eo = page.condOpts[e]
                if (page.condChecked["exclude|" + eo.key] === true && c[eo.key] === true) {
                    keep = false
                    break
                }
            }
            if (keep) {
                for (var n = 0; n < page.condOpts.length; ++n) {
                    var io = page.condOpts[n]
                    if (page.condChecked["include|" + io.key] === true && c[io.key] !== true) {
                        keep = false
                        break
                    }
                }
            }
            if (keep)
                out.push(c)
        }
        return out
    }
    property var kwGroups: ({
        "角色": [
            { t: "地位·称号", tags: [
                { jp: "奴隷", zh: "奴隶" }, { jp: "騎士", zh: "骑士" }, { jp: "貴族", zh: "贵族" },
                { jp: "領主", zh: "领主" }, { jp: "賢者", zh: "贤者" }, { jp: "魔王", zh: "魔王" },
                { jp: "公女", zh: "公主" }, { jp: "冒険者", zh: "冒险者" }, { jp: "勇者", zh: "勇者" },
                { jp: "将官", zh: "将官" }, { jp: "聖女", zh: "圣女" }, { jp: "悪役令嬢", zh: "反派千金" },
                { jp: "王子", zh: "王子" } ] },
            { t: "职业", tags: [
                { jp: "アイドル", zh: "偶像" }, { jp: "侍", zh: "武士" }, { jp: "インフルエンサー", zh: "网红" },
                { jp: "作家", zh: "作家" }, { jp: "配信者", zh: "主播" }, { jp: "家庭教師", zh: "家庭教师" },
                { jp: "ヤクザ", zh: "黑道" }, { jp: "医者", zh: "医生" }, { jp: "探偵", zh: "侦探" },
                { jp: "メイド", zh: "女仆" }, { jp: "記者", zh: "记者" }, { jp: "警察", zh: "警察" },
                { jp: "軍人", zh: "军人" }, { jp: "商人", zh: "商人" }, { jp: "忍者", zh: "忍者" },
                { jp: "パーティ", zh: "队伍" } ] },
            { t: "属性", tags: [
                { jp: "お嬢様", zh: "大小姐" }, { jp: "魔法少女", zh: "魔法少女" }, { jp: "社畜", zh: "社畜" },
                { jp: "殺人鬼", zh: "杀人鬼" }, { jp: "怪盗", zh: "怪盗" }, { jp: "殺し屋", zh: "杀手" },
                { jp: "巫女", zh: "巫女" }, { jp: "陰陽師", zh: "阴阳师" }, { jp: "VTuber", zh: "VTuber" },
                { jp: "主婦", zh: "主妇" }, { jp: "魔女", zh: "魔女" }, { jp: "ハッカー", zh: "黑客" } ] },
            { t: "年龄·性别", tags: [
                { jp: "大学生", zh: "大学生" }, { jp: "中年", zh: "中年" }, { jp: "社会人", zh: "社会人" },
                { jp: "女主人公", zh: "女主角" }, { jp: "おっさん", zh: "大叔" }, { jp: "中学生", zh: "初中生" },
                { jp: "高校生", zh: "高中生" }, { jp: "小学生", zh: "小学生" }, { jp: "男主人公", zh: "男主角" },
                { jp: "老人", zh: "老人" } ] },
            { t: "性格·属性", tags: [
                { jp: "悪役", zh: "反派" }, { jp: "狂気", zh: "疯狂" }, { jp: "チート", zh: "开挂" },
                { jp: "TS", zh: "TS" }, { jp: "異能", zh: "异能" }, { jp: "ブラコン", zh: "兄控" },
                { jp: "ヤンデレ", zh: "病娇" }, { jp: "超能力", zh: "超能力" }, { jp: "最強", zh: "最强" },
                { jp: "脇役", zh: "配角" }, { jp: "天才", zh: "天才" }, { jp: "死にたがり", zh: "求死" },
                { jp: "慎重", zh: "慎重" }, { jp: "無能", zh: "无能" }, { jp: "凡人", zh: "凡人" },
                { jp: "モブ", zh: "路人" } ] },
            { t: "非人类", tags: [
                { jp: "吸血鬼", zh: "吸血鬼" }, { jp: "人外", zh: "异种" }, { jp: "精霊", zh: "精灵" },
                { jp: "AI", zh: "AI" }, { jp: "神", zh: "神" }, { jp: "ドラゴン", zh: "龙" },
                { jp: "幽霊", zh: "幽灵" }, { jp: "妖怪", zh: "妖怪" }, { jp: "宇宙人", zh: "外星人" },
                { jp: "エルフ", zh: "妖精" }, { jp: "女神", zh: "女神" }, { jp: "ロボット", zh: "机器人" },
                { jp: "ゾンビ", zh: "丧尸" }, { jp: "もふもふ", zh: "毛茸茸" }, { jp: "鬼", zh: "鬼" },
                { jp: "猫", zh: "猫" }, { jp: "悪魔", zh: "恶魔" } ] },
            { t: "家族关系", tags: [
                { jp: "祖母", zh: "祖母" }, { jp: "姉", zh: "姐" }, { jp: "義妹", zh: "义妹" },
                { jp: "弟", zh: "弟" }, { jp: "父", zh: "父" }, { jp: "妹", zh: "妹" },
                { jp: "祖父", zh: "祖父" }, { jp: "義理", zh: "义亲" }, { jp: "母", zh: "母" },
                { jp: "孫", zh: "孙" }, { jp: "息子", zh: "儿子" }, { jp: "娘", zh: "女儿" },
                { jp: "兄", zh: "兄" } ] },
            { t: "关系", tags: [
                { jp: "彼氏", zh: "男友" }, { jp: "上司", zh: "上司" }, { jp: "部下", zh: "部下" },
                { jp: "生徒", zh: "学生" }, { jp: "先輩", zh: "前辈" }, { jp: "彼女", zh: "女友" },
                { jp: "社長", zh: "社长" }, { jp: "婚約者", zh: "未婚夫妻" }, { jp: "隣人", zh: "邻居" },
                { jp: "友達", zh: "朋友" }, { jp: "先生", zh: "老师" }, { jp: "夫婦", zh: "夫妇" },
                { jp: "同級生", zh: "同班同学" }, { jp: "許嫁", zh: "婚约对象" }, { jp: "推し", zh: "本命推" },
                { jp: "幼馴染", zh: "青梅竹马" }, { jp: "後輩", zh: "后辈" }, { jp: "同僚", zh: "同事" },
                { jp: "家族", zh: "家人" } ] }
        ],
        "故事": [
            { t: "展开", tags: [
                { jp: "異世界転生", zh: "异世界转生" }, { jp: "復讐", zh: "复仇" }, { jp: "タイムリープ", zh: "时间轮回" },
                { jp: "ハッピーエンド", zh: "圆满结局" }, { jp: "メリーバッドエンド", zh: "苦乐参半结局" },
                { jp: "スローライフ", zh: "慢生活" }, { jp: "ざまあ", zh: "打脸" }, { jp: "貞操逆転", zh: "贞操逆转" },
                { jp: "お仕事", zh: "职场" }, { jp: "裏切り", zh: "背叛" }, { jp: "成り上がり", zh: "逆袭" },
                { jp: "下剋上", zh: "以下克上" }, { jp: "意味がわかると怖い", zh: "细思极恐" },
                { jp: "追放", zh: "流放" }, { jp: "主人公最強", zh: "主角最强" }, { jp: "推理", zh: "推理" },
                { jp: "バッドエンド", zh: "悲惨结局" }, { jp: "成長", zh: "成长" }, { jp: "婚約破棄", zh: "退婚" },
                { jp: "異世界転移", zh: "异世界转移" } ] },
            { t: "氛围", tags: [
                { jp: "シリアス", zh: "严肃" }, { jp: "ギャグ", zh: "搞笑" }, { jp: "ヒューマンドラマ", zh: "人间剧" },
                { jp: "ほのぼの", zh: "温馨" }, { jp: "コメディ", zh: "喜剧" }, { jp: "青春", zh: "青春" },
                { jp: "切ない", zh: "心酸" }, { jp: "ハードボイルド", zh: "硬派" }, { jp: "サスペンス", zh: "悬念" },
                { jp: "日常", zh: "日常" }, { jp: "残酷", zh: "残酷" }, { jp: "感動", zh: "感人" },
                { jp: "ダーク", zh: "暗黑" }, { jp: "ホラー", zh: "恐怖" } ] },
            { t: "舞台", tags: [
                { jp: "迷宮", zh: "迷宫" }, { jp: "ディストピア", zh: "反乌托邦" }, { jp: "山", zh: "山" },
                { jp: "宇宙", zh: "宇宙" }, { jp: "無人島", zh: "无人岛" }, { jp: "ダークファンタジー", zh: "暗黑奇幻" },
                { jp: "ダンジョン", zh: "地下城" }, { jp: "喫茶店", zh: "咖啡店" }, { jp: "中華", zh: "中华" },
                { jp: "後宮", zh: "后宫" }, { jp: "クトゥルフ", zh: "克苏鲁" }, { jp: "学園", zh: "学园" },
                { jp: "魔界", zh: "魔界" }, { jp: "和風", zh: "和风" } ] },
            { t: "主题", tags: [
                { jp: "錬金術", zh: "炼金术" }, { jp: "ミリタリー", zh: "军事" }, { jp: "内政", zh: "内政" },
                { jp: "グルメ", zh: "美食" }, { jp: "デスゲーム", zh: "死亡游戏" }, { jp: "音楽", zh: "音乐" },
                { jp: "怪異", zh: "怪奇" }, { jp: "生産", zh: "生产" }, { jp: "戦記", zh: "战记" },
                { jp: "開拓", zh: "开拓" }, { jp: "配信", zh: "直播" } ] },
            { t: "游戏·科幻", tags: [
                { jp: "ガチャ", zh: "抽卡" }, { jp: "RPG", zh: "RPG" }, { jp: "ソシャゲ", zh: "手游" },
                { jp: "乙女ゲーム", zh: "乙女游戏" }, { jp: "VRMMO", zh: "VRMMO" }, { jp: "ゲーム", zh: "游戏" } ] },
            { t: "恋爱关系", tags: [
                { jp: "遠距離", zh: "异地恋" }, { jp: "オフィスラブ", zh: "办公室恋情" }, { jp: "不倫", zh: "出轨" },
                { jp: "ハーレム", zh: "后宫" }, { jp: "同棲", zh: "同居" }, { jp: "女友達", zh: "女性朋友" },
                { jp: "ガールズラブ", zh: "百合(GL)" }, { jp: "甘々", zh: "甜腻" }, { jp: "イチャイチャ", zh: "黏糊" },
                { jp: "ツンデレ", zh: "傲娇" }, { jp: "夫婦", zh: "夫妇" }, { jp: "三角関係", zh: "三角关系" },
                { jp: "浮気", zh: "花心" }, { jp: "年の差", zh: "年龄差" }, { jp: "片思い", zh: "单相思" },
                { jp: "百合", zh: "百合" }, { jp: "純愛", zh: "纯爱" }, { jp: "溺愛", zh: "溺爱" },
                { jp: "BL", zh: "BL" }, { jp: "初恋", zh: "初恋" }, { jp: "修羅場", zh: "修罗场" },
                { jp: "元彼", zh: "前男友" }, { jp: "嫉妬", zh: "嫉妒" }, { jp: "結婚", zh: "结婚" },
                { jp: "元カノ", zh: "前女友" }, { jp: "離婚", zh: "离婚" }, { jp: "婚約", zh: "婚约" },
                { jp: "ドロドロ", zh: "狗血" }, { jp: "失恋", zh: "失恋" } ] }
        ],
        "其他": [
            { t: "形式", tags: [
                { jp: "モキュメンタリー", zh: "伪纪录片" }, { jp: "都市伝説", zh: "都市传说" },
                { jp: "純文学", zh: "纯文学" }, { jp: "日記", zh: "日记" }, { jp: "時代小説", zh: "时代小说" },
                { jp: "私小説", zh: "私小说" }, { jp: "掲示板", zh: "论坛体" }, { jp: "ショートショート", zh: "超短篇" },
                { jp: "群像劇", zh: "群像剧" }, { jp: "実話", zh: "真实故事" }, { jp: "詩", zh: "诗" },
                { jp: "俳句", zh: "俳句" }, { jp: "童話", zh: "童话" }, { jp: "短歌", zh: "短歌" },
                { jp: "怪談", zh: "怪谈" } ] },
            { t: "时代", tags: [
                { jp: "三国志", zh: "三国志" }, { jp: "第二次世界大戦", zh: "二战" }, { jp: "中世", zh: "中世纪" },
                { jp: "古代", zh: "古代" }, { jp: "縄文", zh: "绳文" }, { jp: "弥生", zh: "弥生" },
                { jp: "奈良", zh: "奈良" }, { jp: "飛鳥", zh: "飞鸟" }, { jp: "平安", zh: "平安" },
                { jp: "鎌倉", zh: "镰仓" }, { jp: "室町", zh: "室町" }, { jp: "南北朝", zh: "南北朝" },
                { jp: "戦国", zh: "战国" }, { jp: "安土桃山", zh: "安土桃山" }, { jp: "江戸", zh: "江户" },
                { jp: "幕末", zh: "幕末" }, { jp: "明治", zh: "明治" }, { jp: "大正", zh: "大正" },
                { jp: "昭和", zh: "昭和" }, { jp: "平成", zh: "平成" }, { jp: "現代", zh: "现代" },
                { jp: "近未来", zh: "近未来" }, { jp: "令和", zh: "令和" } ] },
            { t: "活动", tags: [
                { jp: "第13回角川文庫キャラクター小説大賞", zh: "第13届角川文库角色小说大奖" },
                { jp: "第34回電撃小説大賞", zh: "第34届电击小说大奖" } ] },
            { t: "官方推荐", tags: [
                { jp: "カクヨムオンリー", zh: "KAKUYOMU限定" }, { jp: "グルメ", zh: "美食" },
                { jp: "インターネット文学", zh: "网络文学" }, { jp: "京都", zh: "京都" },
                { jp: "プロマンス", zh: "兄弟情" }, { jp: "怪異", zh: "怪奇" },
                { jp: "バディ", zh: "搭档" }, { jp: "民俗学", zh: "民俗学" } ] }
        ]
    })
    function tagChecked(jp) { return controller.siteKeyword.split(" ").indexOf(jp) >= 0 }

    function loadRanking() {
        controller.siteRanking(page.curGenre, page.curPeriod)
    }
    function genreChecked(jp) {
        return controller.siteSearchGenresSelected.indexOf(jp) >= 0
    }
    // 翻页：仿站点，直接列出页码（当前页黑色加粗，其余蓝色），最多 9 个
    function pageNumbers() {
        var total = controller.sitePageCount
        var cur = controller.sitePage
        var out = []
        if (total <= 1) return out
        // 大页码时减少同时显示的数量，避免检索结果超过千页后挤出中栏。
        var span = Math.max(3, Math.min(9, Math.floor(
            (paginationRow.width - 36)
            / (String(total).length * 10 + 18))))
        var start = Math.max(1, cur - Math.floor(span / 2))
        var end = Math.min(total, start + span - 1)
        start = Math.max(1, end - span + 1)
        for (var i = start; i <= end; ++i) out.push(i)
        return out
    }

    Component.onCompleted: { if (!page.inSearchMode) loadRanking() }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 14
        anchors.bottomMargin: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 196
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    SiteModeSwitcher {
                        Layout.fillWidth: true
                        currentIndex: page.inSearchMode ? 1 : 0
                        searchKind: controller.siteSearchKind
                        onActivated: function(index) {
                            controller.siteSetMode(index === 1 ? "search" : "ranking")
                        }
                        onSearchKindSelected: function(kind) {
                            controller.siteSetSearchKind(kind)
                        }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !page.inSearchMode
                        clip: true
                        contentWidth: width
                        contentHeight: rankCol.height
                        ScrollBar.vertical: ScrollBar { }

                        ColumnLayout {
                            id: rankCol
                            width: parent.width
                            spacing: 4

                            Text { Layout.fillWidth: true; text: "类型"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                            Repeater {
                                model: page.genres
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 26
                                    radius: Theme.radiusSmall
                                    color: page.curGenre === modelData.slug ? Qt.rgba(0.039, 0.518, 1.0, 0.12) : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        spacing: 4
                                        Text { text: page.curGenre === modelData.slug ? "✓" : ""; color: Theme.accent; font.pixelSize: 11; Layout.preferredWidth: 12 }
                                        Text { Layout.fillWidth: true; text: modelData.name; color: page.curGenre === modelData.slug ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: { page.curGenre = modelData.slug; page.loadRanking() } }
                                }
                            }

                            Item { Layout.preferredHeight: 6 }
                            Text { Layout.fillWidth: true; text: "排行周期"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                            Repeater {
                                model: page.periods
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 26
                                    radius: Theme.radiusSmall
                                    color: page.curPeriod === modelData.slug ? Qt.rgba(0.039, 0.518, 1.0, 0.12) : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        spacing: 4
                                        Text { text: page.curPeriod === modelData.slug ? "✓" : ""; color: Theme.accent; font.pixelSize: 11; Layout.preferredWidth: 12 }
                                        Text { Layout.fillWidth: true; text: modelData.name; color: page.curPeriod === modelData.slug ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: { page.curPeriod = modelData.slug; page.loadRanking() } }
                                }
                            }

                            Item { Layout.preferredHeight: 6 }
                            Text { Layout.fillWidth: true; text: "小说长度（按字数）"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                            Repeater {
                                model: page.lengths
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 26
                                    radius: Theme.radiusSmall
                                    color: controller.siteLengthFilterValue === modelData.value ? Qt.rgba(0.039, 0.518, 1.0, 0.12) : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        spacing: 4
                                        Text { text: controller.siteLengthFilterValue === modelData.value ? "✓" : ""; color: Theme.accent; font.pixelSize: 11; Layout.preferredWidth: 12 }
                                        Text { Layout.fillWidth: true; text: modelData.name; color: controller.siteLengthFilterValue === modelData.value ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: controller.siteSetLengthFilter(modelData.value) }
                                }
                            }
                            Item { Layout.preferredHeight: 6 }
                            Text { Layout.fillWidth: true; text: "连载状态"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: page.statusOptions
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 26
                                        radius: Theme.radiusSmall
                                        color: controller.siteStatusFilter === modelData.v ? Qt.rgba(0.039, 0.518, 1.0, 0.16) : "transparent"
                                        border.width: 1
                                        border.color: controller.siteStatusFilter === modelData.v ? Theme.accent : Theme.border
                                        Text { anchors.centerIn: parent; text: modelData.n; color: controller.siteStatusFilter === modelData.v ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 10 }
                                        MouseArea { anchors.fill: parent; onClicked: controller.siteSetStatusFilter(modelData.v) }
                                    }
                                }
                            }
                        }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.inSearchMode
                        clip: true
                        contentWidth: width
                        contentHeight: searchCol.height
                        ScrollBar.vertical: ScrollBar { }

                        ColumnLayout {
                            id: searchCol
                            width: parent.width
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                TextField {
                                    id: kwField
                                    Layout.fillWidth: true
                                    height: 30
                                    placeholderText: page.worksSearch ? "书名・标签" : "作者ID・笔名"
                                    text: controller.siteKeyword
                                    selectByMouse: true
                                    font.family: Theme.uiFont
                                    font.pixelSize: 11
                                    color: Theme.textPrimary
                                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.background; border.width: 1; border.color: Theme.border }
                                    onTextChanged: controller.siteSetKeyword(text)
                                    onAccepted: {
                                        page.openGroup = "" // 检索时收起标签/条件面板
                                        controller.siteSearchNow()
                                    }
                                }
                            }
                            PrimaryButton {
                                Layout.fillWidth: true
                                text: controller.siteSearching ? "检索中…" : "检索"
                                enabled: !controller.siteSearching
                                onClicked: {
                                    page.openGroup = "" // 检索时收起标签/条件面板
                                    controller.siteSearchNow()
                                }
                            }

                            ColumnLayout {
                                id: workSearchFilters
                                Layout.fillWidth: true
                                visible: page.worksSearch
                                spacing: 6

                            Text { Layout.fillWidth: true; text: "排序顺序"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 10 }
                            ComboBox {
                                id: siteSortCombo
                                objectName: "siteSearchOrder"
                                Layout.fillWidth: true
                                model: page.sortOptions
                                textRole: "name"
                                valueRole: "value"
                                currentIndex: {
                                    for (var i = 0; i < page.sortOptions.length; ++i)
                                        if (page.sortOptions[i].value === controller.siteSearchOrder) return i
                                    return 1
                                }
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                                palette.buttonText: Theme.textPrimary
                                background: Rectangle {
                                    implicitHeight: 30
                                    radius: Theme.radiusSmall
                                    color: Theme.background
                                    border.width: 1
                                    border.color: Theme.border
                                }
                                onActivated: controller.siteSetSearchOrder(currentValue)

                                // 展开的下拉列表：90% 不透明度的圆角磨砂玻璃
                                popup: Popup {
                                    id: siteSortPopup
                                    y: siteSortCombo.height - 1
                                    width: siteSortCombo.width
                                    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                                    padding: 4

                                    background: Rectangle {
                                        radius: Theme.radius
                                        color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.90)
                                                          : Qt.rgba(1, 1, 1, 0.90)
                                        border.width: 1
                                        border.color: Theme.glassBorder
                                    }

                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: siteSortPopup.visible ? siteSortCombo.delegateModel : null
                                        currentIndex: siteSortCombo.highlightedIndex
                                        ScrollIndicator.vertical: ScrollIndicator { }
                                    }
                                }
                            }

                            Item { Layout.preferredHeight: 6 }
                            Text { Layout.fillWidth: true; text: "推荐关键词"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                            Repeater {
                                model: ["角色", "故事", "其他"]
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 28
                                    radius: Theme.radiusSmall
                                    color: rowMouse.containsMouse ? Qt.rgba(0, 0, 0, 0.04) : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        Text { Layout.fillWidth: true; text: modelData; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                        Text { text: "\u203A"; color: Theme.textSecondary; font.pixelSize: 14 }
                                    }
                                    MouseArea {
                                        id: rowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: page.openGroup = (page.openGroup === modelData ? "" : modelData)
                                    }
                                }
                            }

                            Item { Layout.preferredHeight: 4 }
                            Text { Layout.fillWidth: true; text: "分类"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                            Repeater {
                                model: page.searchGenres
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 24
                                    implicitHeight: 24
                                    radius: Theme.radiusSmall
                                    color: "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 6
                                        Rectangle {
                                            width: 14
                                            height: 14
                                            radius: 3
                                            color: page.genreChecked(modelData.jp) ? Theme.accent : "transparent"
                                            border.width: 1
                                            border.color: page.genreChecked(modelData.jp) ? Theme.accent : Theme.textSecondary
                                            Text { anchors.centerIn: parent; visible: page.genreChecked(modelData.jp); text: "✓"; color: "white"; font.pixelSize: 9; font.bold: true }
                                        }
                                        Text { Layout.fillWidth: true; text: modelData.zh; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: controller.siteToggleSearchGenre(modelData.jp, !page.genreChecked(modelData.jp))
                                    }
                                }
                            }

                            Item { Layout.preferredHeight: 6 }
                            Text { Layout.fillWidth: true; text: "详细条件"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }

                            Text { Layout.fillWidth: true; text: "连载状态"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 10 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: page.statusOptions
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 24
                                        radius: Theme.radiusSmall
                                        color: controller.siteStatusFilter === modelData.v ? Qt.rgba(0.039, 0.518, 1.0, 0.16) : "transparent"
                                        border.width: 1
                                        border.color: controller.siteStatusFilter === modelData.v ? Theme.accent : Theme.border
                                        Text { anchors.centerIn: parent; text: modelData.n; color: controller.siteStatusFilter === modelData.v ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 10 }
                                        MouseArea { anchors.fill: parent; onClicked: controller.siteSetStatusFilter(modelData.v) }
                                    }
                                }
                            }

                            Text { Layout.fillWidth: true; text: "★ 的数量"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 10 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: page.starOptions
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 24
                                        radius: Theme.radiusSmall
                                        color: controller.siteMinStars === modelData.v ? Qt.rgba(0.039, 0.518, 1.0, 0.16) : "transparent"
                                        border.width: 1
                                        border.color: controller.siteMinStars === modelData.v ? Theme.accent : Theme.border
                                        Text { anchors.centerIn: parent; text: modelData.n; color: controller.siteMinStars === modelData.v ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 9 }
                                        MouseArea { anchors.fill: parent; onClicked: controller.siteSetMinStars(modelData.v) }
                                    }
                                }
                            }

                            Text { Layout.fillWidth: true; text: "小说长度"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 10 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: page.lengths
                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 24
                                        radius: Theme.radiusSmall
                                        color: controller.siteLengthFilterValue === modelData.value ? Qt.rgba(0.039, 0.518, 1.0, 0.16) : "transparent"
                                        border.width: 1
                                        border.color: controller.siteLengthFilterValue === modelData.value ? Theme.accent : Theme.border
                                        Text { anchors.centerIn: parent; text: modelData.name; color: controller.siteLengthFilterValue === modelData.value ? Theme.accent : Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: 10 }
                                        MouseArea { anchors.fill: parent; onClicked: controller.siteSetLengthFilter(modelData.value) }
                                    }
                                }
                            }

                            Repeater {
                                model: page.condGroups
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 30
                                    radius: Theme.radiusSmall
                                    color: condMouse.containsMouse ? Qt.rgba(0, 0, 0, 0.04) : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        Text { Layout.fillWidth: true; text: modelData.label; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                        Text { text: "\u203A"; color: Theme.textSecondary; font.pixelSize: 14 }
                                    }
                                    MouseArea {
                                        id: condMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: page.openGroup = (page.openGroup === modelData.id ? "" : modelData.id)
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "<b>注意</b>：条件筛选为本地筛选，若检索结果内容过少请尝试翻页"
                                textFormat: Text.RichText
                                color: Theme.textSecondary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                            }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    // 标题 + 提示行做成一个固定高度的块：加载提示 / 筛选提示来回切换时
                    // 高度不变，中间列表不会上下跳动（提示行有内容时才占位，不用时不浪费空间）
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(headerTitle.implicitHeight, refreshButton.implicitHeight)
                                               + (page.condActive()
                                                  ? headerCol.spacing + headerNote.implicitHeight : 0)

                        ColumnLayout {
                            id: headerCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                Text {
                                    id: headerTitle
                                    Layout.fillWidth: true
                                    text: controller.siteBusy ? controller.siteMessage
                                          : (controller.siteListTitle.length > 0 ? controller.siteListTitle
                                             : (page.inSearchMode ? "检索结果" : "排行榜"))
                                    color: Theme.textPrimary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.fontBody
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Button {
                                    id: refreshButton
                                    text: "刷新"
                                    leftPadding: 14
                                    rightPadding: 14
                                    implicitHeight: 30
                                    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
                                    hoverEnabled: true
                                    enabled: !controller.siteBusy && !controller.siteOverviewTranslating
                                    opacity: enabled ? 1 : 0.4
                                    scale: down ? 0.96 : 1
                                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                                    background: Rectangle {
                                        radius: Theme.radiusSmall
                                        opacity: refreshButton.down ? 0.8 : refreshButton.hovered ? 0.65 : 0.45
                                        color: refreshButton.down ? Theme.glassFillStrong
                                                                  : Theme.glassButton
                                        LiquidGlass { }
                                        border.width: 1
                                        border.color: refreshButton.visualFocus ? Theme.accent : Theme.glassBorder
                                        Behavior on opacity { NumberAnimation { duration: 120 } }
                                        Behavior on color { ColorAnimation { duration: 120 } }
                                        Behavior on border.color { ColorAnimation { duration: 120 } }
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: Theme.textPrimary
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.fontBody
                                        font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    onClicked: controller.siteRefresh()
                                }
                            }

                            Text {
                                id: headerNote
                                Layout.fillWidth: true
                                visible: page.condActive() && !controller.siteBusy
                                         && page.cardSnapshot.length > 0
                                text: "本地筛选：显示 " + page.filteredCardSnapshot.length + " / "
                                      + page.cardSnapshot.length + " 条"
                                color: Theme.accent
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: page.filteredCardSnapshot
                        spacing: 4

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 72
                            radius: Theme.radiusSmall
                            color: cardMouse.containsMouse ? Qt.rgba(0, 0, 0, 0.04) : "transparent"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                anchors.topMargin: 5
                                anchors.bottomMargin: 5
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.rank || (index + 1)) + ". " + modelData.title
                                    color: Theme.textPrimary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.fontSmall
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        var parts = []
                                        if (modelData.author) parts.push(modelData.author)
                                        if (modelData.genre) parts.push(modelData.genre)
                                        if (modelData.status) parts.push(modelData.status)
                                        return parts.join(" · ")
                                    }
                                    color: Theme.textSecondary
                                    font.family: Theme.uiFont
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        var p = []
                                        if (modelData.follow) p.push("★ " + modelData.follow)
                                        if (modelData.chars) p.push(modelData.chars + "字")
                                        return p.join("   ")
                                    }
                                    color: Theme.accent
                                    font.family: Theme.uiFont
                                    font.pixelSize: 11
                                }
                            }

                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: controller.siteOpenWork(modelData.url)
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !controller.siteBusy && page.filteredCardSnapshot.length === 0
                        text: page.emptyHint()
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        id: paginationRow
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        visible: controller.sitePageCount > 1
                        spacing: 0

                        Item { Layout.fillWidth: true }
                        Repeater {
                            model: page.pageNumbers()
                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.leftMargin: 9
                                Layout.rightMargin: 9
                                text: modelData
                                color: (modelData === controller.sitePage) ? Theme.textPrimary : Theme.accent
                                font.family: Theme.uiFont
                                font.pixelSize: 16
                                font.bold: modelData === controller.sitePage

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -8
                                    cursorShape: Qt.PointingHandCursor
                                    enabled: !controller.siteBusy && modelData !== controller.sitePage
                                    onClicked: controller.siteGoPage(modelData)
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.card
                LiquidGlass { }
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    TextEdit {
                        Layout.fillWidth: true
                        text: controller.siteTitle.length > 0 ? controller.siteTitle : "作品详情"
                        readOnly: true
                        selectByMouse: true
                        color: Theme.textPrimary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                        wrapMode: TextEdit.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: controller.siteChapterCount === 0
                        text: "从中间选择一本作品，这里会显示概要（简介）与详情（小说信息）"
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                    }
                    TextEdit {
                        Layout.fillWidth: true
                        visible: controller.siteChapterCount > 0
                        text: "作者：" + ((controller.siteStats.author && controller.siteStats.author.length > 0)
                                          ? controller.siteStats.author : controller.siteAuthor)
                        readOnly: true
                        selectByMouse: true
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fontSmall
                        wrapMode: TextEdit.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: controller.siteChapterCount > 0
                        spacing: 8

                        CapsuleTabBar {
                            items: ["概要", "详情", "评论"]
                            currentIndex: page.detailTab
                            onActivated: function(index) { page.detailTab = index }
                        }
                        Item { Layout.fillWidth: true }
                        SecondaryButton {
                            enabled: !controller.siteOverviewTranslating
                                     && !(page.detailTab === 2 && controller.siteCommentsBusy)
                            text: page.detailTab === 2
                                  ? (controller.siteOverviewTranslating && controller.siteTranslationKind === "comments"
                                     ? "翻译中…"
                                     : controller.siteCommentsTranslated ? "删除评论" : "翻译评论")
                                  : (controller.siteOverviewTranslated ? "删除概要" : "翻译概要")

                            onClicked: {
                                if (page.detailTab === 2) {
                                    if (controller.siteCommentsTranslated) {
                                        controller.siteDeleteCommentsTranslation()
                                        return
                                    }
                                    if (controller.translationBusy()) {
                                        Navigation.hint("当前有其它翻译任务，请稍后再试")
                                        return
                                    }
                                    if (controller.sakuraRunning || controller.murasakiRunning
                                        || controller.qwenRunning)
                                        Navigation.requestCommentsTranslate()
                                    else
                                        Navigation.requestCommentsNeedsAi()
                                } else {
                                    if (controller.siteOverviewTranslated) {
                                        controller.siteDeleteOverviewTranslation()
                                        return
                                    }
                                    // 已有翻译在跑：不弹确认弹窗，直接在灵动岛提示
                                    if (controller.translationBusy()) {
                                        Navigation.hint("当前有其它翻译任务，请稍后再试")
                                        return
                                    }
                                    if (controller.sakuraRunning || controller.murasakiRunning
                                        || controller.qwenRunning) {
                                        Navigation.requestOverviewTranslate()
                                    } else {
                                        Navigation.requestOverviewNeedsAi()
                                    }
                                }
                            }
                        }
                        SecondaryButton { text: "整本下载"; enabled: !controller.downloading && !controller.siteBusy; onClicked: Navigation.requestDownloadConfirm() }
                        SecondaryButton {
                            text: "去下载页"
                            enabled: !controller.downloading
                            onClicked: Navigation.requestGotoDownload()
                        }
                    }


                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: controller.siteChapterCount > 0 && page.detailTab === 0
                        visible: controller.siteChapterCount > 0 && page.detailTab === 0
                        clip: true
                        contentWidth: width
                        contentHeight: overviewCol.height
                        ScrollBar.vertical: ScrollBar { }
                        ColumnLayout {
                            id: overviewCol
                            width: parent.width
                            spacing: 10

                            // 已翻译：上下对照（译文在上用主色，原文在下用次要色）
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: controller.siteOverviewTranslated
                                spacing: 6
                                Text { Layout.fillWidth: true; text: "译文"; color: Theme.accent; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                                Text {
                                    Layout.fillWidth: true
                                    text: controller.siteOverviewTranslation
                                    color: Theme.textPrimary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.fontBody
                                    lineHeight: 1.6
                                    wrapMode: Text.Wrap
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    Layout.topMargin: 6
                                    Layout.bottomMargin: 6
                                    color: Theme.border
                                }
                                Text { Layout.fillWidth: true; text: "原文"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: 11; font.bold: true }
                                Text {
                                    Layout.fillWidth: true
                                    visible: controller.siteStats.catchphrase !== undefined && controller.siteStats.catchphrase.length > 0
                                    text: controller.siteStats.catchphrase !== undefined ? controller.siteStats.catchphrase : ""
                                    color: Theme.textSecondary
                                    font.family: Theme.uiFont
                                    font.pixelSize: 14
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (controller.siteStats.introduction !== undefined && controller.siteStats.introduction.length > 0)
                                          ? controller.siteStats.introduction : "（本站未提供简介）"
                                    color: Theme.textSecondary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.fontBody
                                    lineHeight: 1.6
                                    wrapMode: Text.Wrap
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: !controller.siteOverviewTranslated
                                spacing: 10
                                Text {
                                    Layout.fillWidth: true
                                    visible: controller.siteStats.catchphrase !== undefined && controller.siteStats.catchphrase.length > 0
                                    text: controller.siteStats.catchphrase !== undefined ? controller.siteStats.catchphrase : ""
                                    color: Theme.textPrimary
                                    font.family: Theme.uiFont
                                    font.pixelSize: 15
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (controller.siteStats.introduction !== undefined && controller.siteStats.introduction.length > 0)
                                          ? controller.siteStats.introduction : "（本站未提供简介）"
                                    color: Theme.textPrimary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.fontBody
                                    lineHeight: 1.6
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: controller.siteChapterCount > 0 && page.detailTab === 1
                        visible: controller.siteChapterCount > 0 && page.detailTab === 1
                        clip: true
                        contentWidth: width
                        contentHeight: detailCol.height
                        ScrollBar.vertical: ScrollBar { }
                        ColumnLayout {
                            id: detailCol
                            width: parent.width
                            spacing: 6
                            Text { Layout.fillWidth: true; text: "小说信息"; color: Theme.textPrimary; font.family: Theme.uiFont; font.pixelSize: Theme.fontBody; font.bold: true }
                            Repeater {
                                model: [
                                    { k: "标题", v: controller.siteStats.title !== undefined ? controller.siteStats.title : "" },
                                    { k: "作者", v: controller.siteStats.author !== undefined ? controller.siteStats.author : "" },
                                    { k: "连载状态", v: controller.siteStats.status !== undefined ? controller.siteStats.status : "" },
                                    { k: "章节数", v: (controller.siteStats.episodeCount !== undefined ? controller.siteStats.episodeCount : "") + " 话" },
                                    { k: "种类", v: controller.siteStats.kind !== undefined ? controller.siteStats.kind : "" },
                                    { k: "内容提示", v: (controller.siteStats.flags !== undefined && controller.siteStats.flags.length > 0) ? controller.siteStats.flags.join(" / ") : "无" },
                                    { k: "总字数", v: (controller.siteStats.charCount !== undefined ? controller.siteStats.charCount : "") + " 字" },
                                    { k: "公开日", v: controller.siteStats.publishedAt !== undefined ? controller.siteStats.publishedAt : "" },
                                    { k: "最终更新", v: controller.siteStats.updatedAt !== undefined ? controller.siteStats.updatedAt : "" }
                                ]
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Text { Layout.preferredWidth: 72; text: modelData.k; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                    TextEdit {
                                        Layout.fillWidth: true
                                        text: modelData.v
                                        readOnly: true
                                        selectByMouse: true
                                        color: Theme.textPrimary
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.fontSmall
                                        wrapMode: TextEdit.Wrap
                                    }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 4; color: Theme.border }
                            Repeater {
                                model: [
                                    { k: "推荐评论", v: "★ " + (controller.siteStats.reviewPoint !== undefined ? controller.siteStats.reviewPoint : "-") },
                                    { k: "应援评论", v: (controller.siteStats.commentCount !== undefined ? controller.siteStats.commentCount : "-") + " 件" },
                                    { k: "小说关注", v: (controller.siteStats.followCount !== undefined ? controller.siteStats.followCount : "-") + " 人" },
                                    { k: "类型", v: controller.siteStats.genre !== undefined ? controller.siteStats.genre : "" }
                                ]
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    Text { Layout.preferredWidth: 72; text: modelData.k; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                    TextEdit {
                                        Layout.fillWidth: true
                                        text: modelData.v
                                        readOnly: true
                                        selectByMouse: true
                                        color: Theme.textPrimary
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.fontSmall
                                        wrapMode: TextEdit.Wrap
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                visible: controller.siteStats.tags !== undefined && controller.siteStats.tags.length > 0
                                Text { Layout.preferredWidth: 72; text: "标签"; color: Theme.textSecondary; font.family: Theme.uiFont; font.pixelSize: Theme.fontSmall }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Repeater {
                                        model: (controller.siteStats.tags !== undefined) ? controller.siteStats.tags : []
                                        Rectangle {
                                            width: tagLabel.implicitWidth + 14
                                            height: 22
                                            radius: 6
                                            color: Theme.glassFillStrong
                                            LiquidGlass { }
                                            border.width: 1
                                            border.color: Theme.border
                                            TextEdit {
                                                id: tagLabel
                                                anchors.centerIn: parent
                                                text: modelData
                                                readOnly: true
                                                selectByMouse: true
                                                color: Theme.textSecondary
                                                font.family: Theme.uiFont
                                                font.pixelSize: 11
                                                wrapMode: TextEdit.NoWrap
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ListView {
                        id: commentsList
                        objectName: "siteCommentsList"
                        Layout.fillWidth: true
                        Layout.fillHeight: controller.siteChapterCount > 0 && page.detailTab === 2
                        visible: controller.siteChapterCount > 0 && page.detailTab === 2
                        clip: true
                        spacing: 10
                        reuseItems: true
                        model: controller.siteComments
                        ScrollBar.vertical: ScrollBar { }
                        header: ColumnLayout {
                            width: commentsList.width
                            spacing: 10
                            Text {
                                Layout.fillWidth: true
                                text: "评论"
                                color: Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                                font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: controller.siteCommentsBusy
                                text: "正在获取评论…"
                                color: Theme.textSecondary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                        delegate: ColumnLayout {
                            width: commentsList.width
                            required property var modelData
                            required property int index
                            spacing: 5
                            Text {
                                Layout.fillWidth: true
                                text: (modelData.author || "匿名")
                                      + (modelData.date ? " · " + modelData.date : "")
                                      + (modelData.point !== undefined ? " · ★" + modelData.point : "")
                                      + (modelData.likeCount !== undefined ? " · いいね " + modelData.likeCount : "")
                                      + (modelData.isSpoiler ? " · 剧透" : "")
                                color: Theme.textSecondary
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                visible: modelData.title !== undefined && modelData.title.length > 0
                                text: modelData.title || ""
                                readOnly: true
                                selectByMouse: true
                                color: Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                                wrapMode: TextEdit.Wrap
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                text: modelData.body
                                readOnly: true
                                selectByMouse: true
                                color: Theme.textPrimary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontSmall
                                wrapMode: TextEdit.Wrap
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                visible: controller.siteCommentTranslations[index] !== undefined
                                         && controller.siteCommentTranslations[index].length > 0
                                text: controller.siteCommentTranslations[index] || ""
                                readOnly: true
                                selectByMouse: true
                                color: Theme.accent
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontSmall
                                wrapMode: TextEdit.Wrap
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                Layout.bottomMargin: 4
                                Layout.preferredHeight: 1
                                color: Theme.border
                            }
                        }
                        footer: Text {
                            width: commentsList.width
                            visible: !controller.siteCommentsBusy && controller.siteComments.length === 0
                            text: "暂无评论"
                            color: Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fontSmall
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: controller.siteChapterCount === 0
                    }

                }
            }
        }

    }

    // ---- 关键词分类面板：点左侧「推荐关键词」后向右展开（照站点样式）----
    // 面板之外点击：关闭当前面板。
    // 只覆盖左栏右侧区域：左栏保持可点，方便直接切换到别的分类 / 条件。
    MouseArea {
        anchors.left: parent.left
        anchors.leftMargin: 226
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: 40
        visible: page.openGroup !== ""
        onClicked: page.openGroup = ""
    }

    Rectangle {
        id: kwPanel
        visible: page.openGroup !== ""
        // 「只保留含有 / 排除含有」内容很少：只占左下角一块够用的地方；
        // 标签（角色 / 故事 / 其他）内容多，仍占右侧大块。
        readonly property bool condPanel: page.isCondGroup(page.openGroup)
        x: 226
        y: condPanel ? Math.max(14, parent.height - height - 26) : 14
        width: condPanel ? Math.min(340, parent.width - 246)
                         : Math.max(200, parent.width - 246)
        height: condPanel ? Math.min(condCol.implicitHeight + 84, parent.height - 60)
                          : Math.max(120, parent.height - 40)
        z: 50
        radius: Theme.radius
        // 80% 不透明度磨砂玻璃
        color: Theme.dark ? Qt.rgba(0.11, 0.11, 0.12, 0.80) : Qt.rgba(1, 1, 1, 0.80)
        border.width: 1
        border.color: Theme.glassBorder

        // 面板自身吞掉点击（含空白处），避免点面板内部反而把面板关掉
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: page.groupLabel(page.openGroup)
                    color: Theme.textPrimary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }
                SecondaryButton {
                    text: "关闭"
                    onClicked: page.openGroup = ""
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: groupCol.height
                ScrollBar.vertical: ScrollBar { }

                ColumnLayout {
                    id: groupCol
                    width: parent.width
                    spacing: 12

                    ColumnLayout {
                        id: condCol
                        Layout.fillWidth: true
                        spacing: 10
                        visible: page.isCondGroup(page.openGroup)
                        Text {
                            Layout.fillWidth: true
                            text: page.condHint(page.openGroup)
                            color: Theme.textSecondary
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: page.condOpts
                            Rectangle {
                                Layout.fillWidth: true
                                height: 40
                                radius: 6
                                color: page.condOn(modelData.key) ? Qt.rgba(0.039, 0.518, 1.0, 0.14) : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 10
                                    Rectangle {
                                        width: 20
                                        height: 20
                                        radius: 4
                                        color: page.condOn(modelData.key) ? Theme.accent : "transparent"
                                        border.width: 1
                                        border.color: page.condOn(modelData.key) ? Theme.accent : Theme.textSecondary
                                        Text { anchors.centerIn: parent; visible: page.condOn(modelData.key); text: "✓"; color: "white"; font.pixelSize: 12; font.bold: true }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.zh
                                        color: Theme.textPrimary
                                        font.family: Theme.uiFont
                                        font.pixelSize: 14
                                        wrapMode: Text.Wrap
                                    }
                                }
                                MouseArea { anchors.fill: parent; onClicked: page.toggleCond(modelData.key) }
                            }
                        }
                    }

                    Repeater {
                        model: page.isCondGroup(page.openGroup)
                               ? [] : ((page.kwGroups[page.openGroup] !== undefined) ? page.kwGroups[page.openGroup] : [])
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: modelData.t
                                color: Theme.textSecondary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.fontBody
                                font.bold: true
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Repeater {
                                    model: modelData.tags
                                    Rectangle {
                                        width: chipRow.implicitWidth + 30
                                        height: 40
                                        radius: 6
                                        color: page.tagChecked(modelData.jp) ? Qt.rgba(0.039, 0.518, 1.0, 0.14) : Theme.glassFillStrong
                                        border.width: 1
                                        border.color: page.tagChecked(modelData.jp) ? Theme.accent : Theme.border
                                        Row {
                                            id: chipRow
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Text {
                                                id: chipMark
                                                height: 22
                                                width: 18
                                                // 始终用「＋」，选中时旋转 45° 并放大——视觉上就是「✕」
                                                text: "\uFF0B"
                                                color: page.tagChecked(modelData.jp) ? Theme.accent : Theme.textSecondary
                                                font.pixelSize: 15
                                                verticalAlignment: Text.AlignVCenter
                                                horizontalAlignment: Text.AlignHCenter
                                                rotation: page.tagChecked(modelData.jp) ? 45 : 0
                                                scale: page.tagChecked(modelData.jp) ? 1.25 : 1.0
                                                Behavior on rotation {
                                                    NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 1.5 }
                                                }
                                                Behavior on scale {
                                                    NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 1.5 }
                                                }
                                            }
                                            Text {
                                                id: chipLabel
                                                height: 22
                                                text: modelData.zh
                                                color: page.tagChecked(modelData.jp) ? Theme.accent : Theme.textPrimary
                                                font.family: Theme.uiFont
                                                font.pixelSize: 14
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: controller.siteToggleSearchKeyword(modelData.jp, !page.tagChecked(modelData.jp))
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !page.isCondGroup(page.openGroup)
                        text: "选中即把该标签的原版日文加入搜索框；点「✕」可从搜索框移除。"
                        color: Theme.textSecondary
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
