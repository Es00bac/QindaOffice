// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Hand-written HTML for tst_mail_html (M64): an Outlook-shaped and a
// Gmail-shaped message and a hostile corpus. No real mail: every string here
// was written for the test.

namespace MailHtmlSamples {

// Outlook / Word "filtered medium" HTML: conditional comments, <xml> islands,
// MsoNormal classes, <o:p>, @font-face and @page in the head, a cid image.
inline constexpr const char *kOutlook = R"html(<html xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office" xmlns:w="urn:schemas-microsoft-com:office:word" xmlns="http://www.w3.org/TR/REC-html40"><head><meta http-equiv=Content-Type content="text/html; charset=utf-8"><meta name=Generator content="Microsoft Word 15 (filtered medium)"><!--[if !mso]><style>v\:* {behavior:url(#default#VML);}
o\:* {behavior:url(#default#VML);}
</style><![endif]--><style><!--
/* Font Definitions */
@font-face
	{font-family:"Cambria Math";
	panose-1:2 4 5 3 5 4 6 3 2 4;}
/* Style Definitions */
p.MsoNormal, li.MsoNormal, div.MsoNormal
	{margin:0in;
	font-size:11.0pt;
	font-family:"Calibri",sans-serif;}
a:link, span.MsoHyperlink
	{mso-style-priority:99;
	color:#0563C1;
	text-decoration:underline;}
span.EmailStyle17
	{mso-style-type:personal-compose;
	color:windowtext;}
@page WordSection1
	{size:8.5in 11.0in;
	margin:1.0in 1.0in 1.0in 1.0in;}
div.WordSection1
	{page:WordSection1;}
--></style><!--[if gte mso 9]><xml>
<o:shapedefaults v:ext="edit" spidmax="1026" />
</xml><![endif]--></head><body lang=EN-US link="#0563C1" vlink="#954F72" style='word-wrap:break-word'><div class=WordSection1><p class=MsoNormal>Hi Ada,<o:p></o:p></p><p class=MsoNormal><o:p>&nbsp;</o:p></p><p class=MsoNormal>The quarterly numbers are attached &#8211; see the chart below.<o:p></o:p></p><p class=MsoNormal><img width=320 height=200 style='width:3.3333in;height:2.0833in' id="Picture_x0020_1" src="cid:image001.png@01DA1234.56789AB0" alt="Chart"><o:p></o:p></p><table class=MsoNormalTable border=0 cellspacing=0 cellpadding=0><tr><td width=200 valign=top style='width:150.0pt;padding:0in 5.4pt 0in 5.4pt'><p class=MsoNormal>Revenue<o:p></o:p></p></td><td width=200 valign=top style='width:150.0pt;padding:0in 5.4pt 0in 5.4pt'><p class=MsoNormal>&euro;1.2M<o:p></o:p></p></td></tr></table><p class=MsoNormal><span class=EmailStyle17>Regards,<br>Bob</span><o:p></o:p></p></div></body></html>)html";

// Gmail: dir="ltr" wrappers, a gmail_quote blockquote, a tracking pixel.
inline constexpr const char *kGmail = R"html(<div dir="ltr">Thanks, that works for me.<div><br></div><div>Ada</div></div><br><div class="gmail_quote"><div dir="ltr" class="gmail_attr">On Mon, 5 Oct 2026 at 09:12, Bob Example &lt;<a href="mailto:bob@example.org">bob@example.org</a>&gt; wrote:<br></div><blockquote class="gmail_quote" style="margin:0px 0px 0px 0.8ex;border-left:1px solid rgb(204,204,204);padding-left:1ex"><div dir="ltr">Does Tuesday work?<br><img src="https://ci3.example.com/meips/track.gif" width="1" height="1" alt=""></div></blockquote></div>)html";

struct HostileCase {
    const char *name;
    const char *html;
    const char *forbidden; // '|'-separated, lower case, must not appear in the output
    const char *expected;  // must appear in the rendered text ("" = no check)
};

inline constexpr HostileCase kHostile[] = {
    {"script", "<p>a</p><script>alert(1)</script><p>b</p>", "<script|alert", "b"},
    {"script src", "<SCRIPT SRC=\"http://evil.example/x.js\"></SCRIPT><p>ok</p>", "script|evil", "ok"},
    {"style expression", "<style>p{width:expression(alert(1))}</style><p>t</p>", "expression|alert", "t"},
    {"iframe", "<iframe src=\"http://evil.example/\"></iframe><p>t</p>", "iframe|evil", "t"},
    {"iframe srcdoc", "<iframe srcdoc=\"<script>alert(1)</script>\">x</iframe>t", "iframe|srcdoc|alert", "t"},
    {"object embed", "<object data=\"http://evil.example/x.swf\"><param name=a value=b><embed src=\"http://evil.example/y\">fallback</object>t", "object|param|embed|evil|fallback", "t"},
    {"embed javascript", "<embed src=\"javascript:alert(1)\">t", "embed|javascript", "t"},
    {"applet", "<applet code=\"X.class\">fallback</applet>t", "applet|fallback", "t"},
    {"svg onload", "<svg onload=\"alert(1)\"><circle r=\"5\"/></svg>t", "svg|onload|alert|circle", "t"},
    {"svg script", "<svg><script>alert(1)</script></svg>t", "svg|script|alert", "t"},
    {"svg self-closing", "<svg/><p>after</p>", "svg", "after"},
    {"svg style mxss", "<svg><style><img src=x onerror=alert(1)></style></svg>t", "onerror|alert", "t"},
    {"math", "<math><mtext><img src=x onerror=alert(1)></mtext></math>t", "math|onerror|alert", "t"},
    {"onerror unquoted", "<img src=x onerror=alert(1)>t", "onerror|alert|<img", "t"},
    {"onload double quotes", "<body ONLOAD=\"alert(1)\"><p>t</p></body>", "onload|alert", "t"},
    {"onmouseover single quotes", "<a href=\"https://example.org/\" onmouseover='alert(1)'>link</a>", "onmouseover|alert", "link"},
    {"slash separated", "<img/src=\"https://x.example/i.png\"/onerror=alert(1)>t", "onerror|alert|x.example", "t"},
    {"slashes unquoted", "<img/src=x/onerror=alert(1)>t", "onerror|<img", "t"},
    {"javascript href", "<a href=\"javascript:alert(1)\">x</a>", "javascript|href", "x"},
    {"decimal entity", "<a href=\"&#106;avascript:alert(1)\">x</a>", "avascript|href", "x"},
    {"padded entities", "<a href=\"&#0000106&#0000097vascript:alert(1)\">x</a>", "vascript|href", "x"},
    {"tab entity", "<a href=\"jav&#x09;ascript:alert(1)\">x</a>", "ascript|href", "x"},
    {"newline entity", "<a href=\"java&#10;script:alert(1)\">x</a>", "script|href", "x"},
    {"colon entity", "<a href=\"javascript&colon;alert(1)\">x</a>", "javascript|href", "x"},
    {"leading spaces", "<a href=\"  javascript:alert(1)\">x</a>", "javascript|href", "x"},
    {"upper case", "<a href=\" JAVASCRIPT:alert(1)\">x</a>", "javascript|href", "x"},
    {"spaced scheme", "<a href=\"java script:alert(1)\">x</a>", "script|href", "x"},
    {"zero width", "<a href=\"java\u200Bscript:alert(1)\">x</a>", "script:|href", "x"},
    {"vbscript", "<a href=\"vbscript:msgbox(1)\">x</a>", "vbscript|href", "x"},
    {"data html link", "<a href=\"data:text/html;base64,PHNjcmlwdD5hbGVydCgxKTwvc2NyaXB0Pg==\">x</a>", "data:|href", "x"},
    {"data html image", "<img src=\"data:text/html,<script>alert(1)</script>\">t", "data:|<script|<img", "t"},
    {"unterminated comment", "<p>shown</p><!-- <script>alert(1)</script>", "script|alert", "shown"},
    {"unterminated tag", "<p>shown</p><img src=\"https://t.example/p.gif\" onerror=\"alert(1)", "onerror|t.example|<img", "shown"},
    {"dangling quote", "<p>shown</p><img src='https://evil.example/?", "evil|<img", "shown"},
    {"split script", "<scr<script>ipt>alert(1)</script>", "<scr|<script", ""},
    {"script end in string", "<script>var s = \"</script\"; alert(1)</script><p>after</p>", "alert|script", "after"},
    {"quotes in attributes", "<div title=\"&quot;><script>alert(1)</script>\">t</div>", "<script", "t"},
    {"end tag in attribute", "<p title=\"</p><script>alert(1)</script>\">t</p>", "<script", "t"},
    {"style import", "<style>@import url(http://evil.example/x.css); p{color:red}</style><p>t</p>", "import|evil", "t"},
    {"style url", "<p style=\"background:url(http://track.example/x.gif); color: red\">t</p>", "url(|track", "t"},
    {"style expression attribute", "<p style=\"width: expression(alert(1)); color: blue\">t</p>", "expression|alert", "t"},
    {"css escapes", "<p style=\"background: \\75 rl(http://evil.example/x)\">t</p><style>p{background:u\\72l(http://evil.example/y)}</style>", "evil|\\", "t"},
    {"behavior", "<p style=\"behavior: url(x.htc)\">t</p>", "behavior|htc", "t"},
    {"moz binding", "<p style=\"-moz-binding: url(http://evil.example/x.xml#y)\">t</p>", "binding|evil", "t"},
    {"position fixed", "<div style=\"position:fixed;top:0\">overlay</div>", "fixed", "overlay"},
    {"base href", "<base href=\"http://evil.example/\"><a href=\"x\">t</a>", "base|evil|href", "t"},
    {"meta refresh", "<meta http-equiv=\"refresh\" content=\"0;url=http://evil.example/\">t", "meta|refresh|evil", "t"},
    {"link stylesheet", "<link rel=\"stylesheet\" href=\"http://evil.example/x.css\">t", "link|evil", "t"},
    {"form", "<form action=\"http://evil.example/\"><input name=\"q\" value=\"v\"><button formaction=\"http://evil.example/2\">Go</button><select><option>o1</option></select><textarea>secret</textarea></form>", "form|input|evil|select|o1|textarea|secret", "Go"},
    {"frameset", "<frameset><frame src=\"http://evil.example/\"></frameset>t", "frame|evil", "t"},
    {"template", "<template><img src=x onerror=alert(1)></template>t", "template|onerror", "t"},
    {"media", "<video poster=\"http://evil.example/p.jpg\" src=\"http://evil.example/v.mp4\">v</video><audio src=\"http://evil.example/a.mp3\">a</audio>t", "video|audio|evil|poster", "t"},
    {"srcset", "<img src=\"cid:a@b\" srcset=\"http://evil.example/1x.png 1x\">", "srcset|evil", ""},
    {"background attribute", "<table background=\"http://evil.example/bg.png\"><tr><td>t</td></tr></table>", "background|evil", "t"},
    {"xlink", "<a xlink:href=\"javascript:alert(1)\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">t</a>", "xlink|javascript|xmlns", "t"},
    {"link attributes", "<a href=\"https://e.example/\" target=\"_blank\" rel=\"noopener\" ping=\"http://evil.example/\" data-x=\"1\">t</a>", "target|rel=|ping|evil|data-", "t"},
    {"processing instructions", "<?xml version=\"1.0\"?><?import namespace=\"t\" implementation=\"#default#time2\"><p>t</p>", "import|xml|time2", "t"},
    {"obsolete", "<keygen name=k><isindex action=\"javascript:alert(1)\"><bgsound src=\"http://evil.example/s\">t", "keygen|isindex|javascript|bgsound|evil", "t"},
    {"conditional comment", "<!--[if gte mso 9]><script>alert(1)</script><![endif]-->t", "script|alert", "t"},
    {"bogus comment", "<! <img src=x onerror=alert(1)> >t", "onerror|alert", "t"},
    {"cdata", "<![CDATA[<script>alert(1)</script>]]>t", "script|alert", "t"},
    {"textarea breakout", "<textarea></textarea><img src=x onerror=alert(1)>t", "onerror|textarea", "t"},
    {"title breakout", "<title></title><img src=x onerror=alert(1)>t", "onerror", "t"},
    {"noembed", "<noembed><img src=x onerror=alert(1)></noembed>t", "noembed|onerror", "t"},
    {"file and relative images", "<img src=\"file:///etc/passwd\"><img src=\"/home/user/x.png\"><img src=\"\\\\host\\share\\x.png\">t", "file:|passwd|home|share|<img", "t"},
};

} // namespace MailHtmlSamples
