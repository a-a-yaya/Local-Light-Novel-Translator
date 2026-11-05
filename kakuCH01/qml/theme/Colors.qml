pragma Singleton
import QtQuick

// 纯白界面背景；按钮与组件使用磨砂玻璃（近白 + 发丝描边 + 柔和投影）。
QtObject {
    // 亮色主题（默认：普通纯白背景）
    readonly property color lightBackground: "#FFFFFF"
    readonly property color lightTextPrimary: "#1C1C1E"
    readonly property color lightTextSecondary: "#6E6E73"
    readonly property color lightAccent: "#0A84FF"
    readonly property color lightGlassFill: "#66E9F2FF"        // 卡片/面板：透明液体玻璃（冷调）
    readonly property color lightGlassFillStrong: "#80E6F0FF"  // 顶栏/侧栏：稍强但仍透明
    readonly property color lightGlassButton: "#CCEAF1FA"      // 有文字的按钮：更“毛”更不透明
    readonly property color lightGlassBorder: "#A6C2D2E6"      // 白底上可见的冷调发丝描边
    readonly property color lightGlassHighlight: "#FFFFFF"

    // 暗色主题
    readonly property color darkBackground: "#0A0A0C"
    readonly property color darkTextPrimary: "#F2F2F7"
    readonly property color darkTextSecondary: "#AEAEB2"
    readonly property color darkAccent: "#0A84FF"
    readonly property color darkGlassFill: "#12FFFFFF"       // 7% 白
    readonly property color darkGlassFillStrong: "#1FFFFFFF" // 12% 白
    readonly property color darkGlassButton: "#40FFFFFF"     // 25% 白
    readonly property color darkGlassBorder: "#33FFFFFF"
    readonly property color darkGlassHighlight: "#2EFFFFFF"

    // 状态色
    readonly property color success: "#34C759"
    readonly property color warning: "#FF9F0A"
    readonly property color danger: "#FF453A"

    // 阅读器主题（不随全局明暗变化）
    readonly property color sepiaBackground: "#F5EAD8"
    readonly property color sepiaText: "#5B4636"
    readonly property color blackBackground: "#000000"
}