# 生成 WebAssets.h：把 settings.html / settings.css / settings.js 内嵌为
# C++ 原始字符串字面量，并提供 BuildSettingsHtml() 把 CSS/JS 内联进 HTML。
# 不依赖 .rc / 资源编译器（本项目明确禁止 .rc），保持单文件交付。
$srcDir = 'E:\odbuild\ipad-screen\src\app\web'
$html = Get-Content "$srcDir\settings.html" -Raw -Encoding UTF8
$css  = Get-Content "$srcDir\settings.css"  -Raw -Encoding UTF8
$js   = Get-Content "$srcDir\settings.js"   -Raw -Encoding UTF8

# 分隔符冲突检查
$d1 = ')odhtml"'
$d2 = ')odcss"'
$d3 = ')odjs"'
if ($html.Contains($d1) -or $css.Contains($d2) -or $js.Contains($d3)) {
  throw '内容包含原始字面量分隔符，生成中止'
}

$header = @'
// 自动生成：由 tools/gen_webassets.ps1 从 src/app/web/* 生成，勿手改。
// 将本地 HTML/CSS/JS 以原始字符串字面量内嵌进可执行文件，保持单文件交付，
// 避免使用 .rc / 资源编译器（本项目明确禁止 .rc）。
#pragma once

#include <string>

namespace od {

inline const char* kSettingsHtml = R"odhtml(
'@

$mid1 = @'
)odhtml";

inline const char* kSettingsCss = R"odcss(
'@

$mid2 = @'
)odcss";

inline const char* kSettingsJs = R"odjs(
'@

$tail = @'
)odjs";

// 把 CSS 与 JS 内联进 HTML，返回可直接传给 NavigateToString 的完整页面。
inline std::string BuildSettingsHtml()
{
    std::string html = kSettingsHtml;
    const std::string css = kSettingsCss;
    const std::string js = kSettingsJs;

    const std::string cssLink = "<link rel=\"stylesheet\" href=\"settings.css\" />";
    std::string style = "<style>\n" + css + "\n</style>";
    size_t pos = html.find(cssLink);
    if (pos != std::string::npos)
        html.replace(pos, cssLink.size(), style);

    const std::string jsTag = "<script src=\"settings.js\"></script>";
    std::string script = "<script>\n" + js + "\n</script>";
    pos = html.find(jsTag);
    if (pos != std::string::npos)
        html.replace(pos, jsTag.size(), script);

    return html;
}

} // namespace od
'@

$out = $header + $html + $mid1 + $css + $mid2 + $js + $tail
# 统一换行为 LF
$out = $out -replace "`r`n", "`n"
Set-Content -Path "$srcDir\..\WebAssets.h" -Value $out -Encoding UTF8
Write-Output "已生成 src/app/WebAssets.h"
