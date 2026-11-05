pragma Singleton
import QtQuick
import Qt.labs.settings
import KakuCH

// 主题单例：根据 dark 开关动态提供颜色、玻璃表面、字体与间距。
QtObject {
    id: theme
    property bool dark: false
    // 放在主题单例中读写，启动时恢复且不依赖是否进入设置页。
    property QtObject appearanceSettings: Settings {
        category: "Appearance"
        property alias dark: theme.dark
    }
    property url backgroundImage: ""
    readonly property bool customBackground: backgroundImage.toString() !== ""
    readonly property color pageBackground: customBackground ? "transparent" : background

    // 组件对背景的遮挡程度：设置页可调（0%~100%，10% 一档），统一作用于常规圆角组件
    // （卡片 / 输入框 / 次要按钮 / 胶囊 / 描边）。
    //   0   = 保持原有玻璃观感（背景能透出来）
    //   100 = 组件完全不透明，彻底挡住底下的背景（自定义背景图时保证可读性）
    // 各组件的可见颜色在两端之间保持不变，变的只是"挡不挡得住背景"。
    // 悬浮展开的面板（标签面板、模型选择面板、确认弹窗）用各自固定的透明度，不受影响。
    // 持久化由 Main.qml / SettingsPage.qml 里的 Settings 读写（与背景图同一套做法）。
    property real componentAlpha: 0.2

    // 把半透明的组件色换算成"同等观感但更能遮住背景"的颜色：
    // 先把原色压在页面底色上得到不变的不透明结果，再按 componentAlpha 提升 alpha，
    // 同时反解出对应的 rgb，保证可见颜色始终一致。
    function dim(c) {
        // 0%（默认）时直接返回原色：组件自身透明度与加入这个开关之前完全一致
        if (componentAlpha <= 0.0001)
            return c
        const b = background
        const t = Math.max(0, Math.min(1, componentAlpha))
        const a = c.a + (1 - c.a) * t
        if (a <= 0.0001)
            return Qt.rgba(c.r, c.g, c.b, 0)
        const sr = b.r * (1 - c.a) + c.r * c.a
        const sg = b.g * (1 - c.a) + c.g * c.a
        const sb = b.b * (1 - c.a) + c.b * c.a
        const clamp01 = function (v) { return Math.max(0, Math.min(1, v)) }
        return Qt.rgba(clamp01((sr - b.r * (1 - a)) / a),
                       clamp01((sg - b.g * (1 - a)) / a),
                       clamp01((sb - b.b * (1 - a)) / a),
                       a)
    }

    // 组件配色：0 = 默认暖纱，保留独立暖纱选项；其它选项按色相生成，
    // 这样四个玻璃面（填充 / 强填充 / 按钮 / 描边）的层次关系保持不变，只换色调。
    property int componentTint: 0
    readonly property bool liquidGlass: componentTint === 7
    // 主窗口提供独立背景纹理；不会采样组件自身，避免反馈和重复绘制。
    property var glassBackdrop: null
    property var glassTexture: null
    readonly property var componentTints: [
        { name: "默认", hue: 30, saturation: 0.28 },
        { name: "天蓝", hue: 205 },
        { name: "青蓝", hue: 190 },
        { name: "青绿", hue: 162 },
        { name: "靛紫", hue: 266 },
        { name: "樱粉", hue: 335 },
        { name: "琥珀", hue: 36 },
        { name: "液态玻璃", hue: -1 },
        { name: "鼠尾草", hue: 145, saturation: 0.32 },
        { name: "雾紫", hue: 275, saturation: 0.30 },
        { name: "暖纱", hue: 30, saturation: 0.28 }
    ]

    function hueShiftAt(c, t) {
        if (t < 0 || t >= componentTints.length)
            return c
        if (componentTints[t].hue < 0)
            return Qt.rgba(c.r, c.g, c.b, c.a)
        const mx = Math.max(c.r, c.g, c.b)
        const mn = Math.min(c.r, c.g, c.b)
        const l = (mx + mn) / 2
        let s = 0
        if (mx !== mn) {
            const d = mx - mn
            s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn)
        }
        if (componentTints[t].saturation !== undefined)
            s *= componentTints[t].saturation
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s
        const p = 2 * l - q
        const hue2rgb = function (pp, qq, tt) {
            let u = tt
            if (u < 0) u += 1
            if (u > 1) u -= 1
            if (u < 1 / 6) return pp + (qq - pp) * 6 * u
            if (u < 1 / 2) return qq
            if (u < 2 / 3) return pp + (qq - pp) * (2 / 3 - u) * 6
            return pp
        }
        const hh = componentTints[t].hue / 360
        return Qt.rgba(hue2rgb(p, q, hh + 1 / 3), hue2rgb(p, q, hh), hue2rgb(p, q, hh - 1 / 3), c.a)
    }

    function hueShift(c) {
        return hueShiftAt(c, componentTint)
    }

    // 设置页色板预览用的颜色（不透明版本）
    function tintPreview(i) {
        if (i === 7)
            return dark ? "#667078" : "#DEE5E9"
        return hueShiftAt(Qt.rgba(0.7608, 0.8235, 0.9020, 1), i)
    }

    readonly property color background: dark ? Colors.darkBackground : Colors.lightBackground
    readonly property color textPrimary: dark ? Colors.darkTextPrimary : Colors.lightTextPrimary
    readonly property color textSecondary: dark ? Colors.darkTextSecondary : Colors.lightTextSecondary
    readonly property color accent: dark ? Colors.darkAccent : Colors.lightAccent

    // 强调色：桌面 hover 保持同色，按下更深，避免无意义的 hover 动效
    readonly property color accentHover: accent
    readonly property color accentPressed: dark ? "#2B8CFF" : "#0068D4"

    // 磨砂玻璃表面
    readonly property color glassFill: dim(liquidGlass ? (dark ? "#14212730" : "#14FFFFFF") : hueShift(dark ? Colors.darkGlassFill : Colors.lightGlassFill))
    readonly property color glassFillStrong: dim(liquidGlass ? (dark ? "#24212730" : "#26FFFFFF") : hueShift(dark ? Colors.darkGlassFillStrong : Colors.lightGlassFillStrong))
    readonly property color glassButton: dim(liquidGlass ? (dark ? "#40212730" : "#40FFFFFF") : hueShift(dark ? Colors.darkGlassButton : Colors.lightGlassButton))
    readonly property color glassBorder: dim(liquidGlass ? (dark ? "#50FFFFFF" : "#30667788") : hueShift(dark ? Colors.darkGlassBorder : Colors.lightGlassBorder))
    readonly property color glassHighlight: dim(hueShift(dark ? Colors.darkGlassHighlight : Colors.lightGlassHighlight))
    readonly property color glassShadow: dark ? "#70000000" : "#3D000000"

    // 兼容旧命名的别名
    readonly property color sidebar: glassFillStrong
    readonly property color card: glassFill
    readonly property color border: glassBorder

    // 圆角与间距
    readonly property int railWidth: 48
    readonly property int radiusLarge: 24
    readonly property int radius: 16
    readonly property int radiusSmall: 10
    readonly property int spacing: 12
    readonly property int spacingLarge: 20

    // 字体：正文无衬线，页面大标题用衬线形成文学感
    readonly property string displayFont: "Noto Serif SC"
    readonly property string uiFont: "Noto Sans SC"

    // 字号
    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 20
    readonly property int fontHeading: 30
}
