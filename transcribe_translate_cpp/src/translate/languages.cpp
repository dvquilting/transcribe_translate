#include "languages.h"

#include <algorithm>
#include <cctype>

namespace tl {

const std::vector<Language>& languages() {
    // Union of the languages TranslateGemma (55) and Hy-MT2 (38) were trained on,
    // plus Spanish as spoken in Spain and Haitian Creole. Qwen 3.5 handles all of
    // them to some degree. Listed alphabetically by English name (menu order);
    // a variant is "Language (country or script)" so it sorts with its language.
    static const std::vector<Language> list = [] {
        std::vector<Language> v = {
            {"ar", "Arabic", "العربية", "阿拉伯语"},
            {"bn", "Bengali", "বাংলা", "孟加拉语"},
            {"bg", "Bulgarian", "Български", "保加利亚语"},
            {"my", "Burmese", "မြန်မာ", "缅甸语"},
            {"yue", "Cantonese", "粵語", "粤语"},
            {"ca", "Catalan", "Català", "加泰罗尼亚语"},
            {"zh", "Chinese (Simplified)", "简体中文", "简体中文"},
            {"zh-Hant", "Chinese (Traditional)", "繁體中文", "繁体中文"},
            {"hr", "Croatian", "Hrvatski", "克罗地亚语"},
            {"cs", "Czech", "Čeština", "捷克语"},
            {"da", "Danish", "Dansk", "丹麦语"},
            {"nl", "Dutch", "Nederlands", "荷兰语"},
            {"en", "English", "English", "英语"},
            {"et", "Estonian", "Eesti", "爱沙尼亚语"},
            {"fil", "Filipino", "Filipino", "菲律宾语"},
            {"fi", "Finnish", "Suomi", "芬兰语"},
            {"fr", "French", "Français", "法语"},
            {"de", "German", "Deutsch", "德语"},
            {"el", "Greek", "Ελληνικά", "希腊语"},
            {"gu", "Gujarati", "ગુજરાતી", "古吉拉特语"},
            {"ht", "Haitian Creole", "Kreyòl ayisyen", "海地克里奥尔语"},
            {"he", "Hebrew", "עברית", "希伯来语"},
            {"hi", "Hindi", "हिन्दी", "印地语"},
            {"hu", "Hungarian", "Magyar", "匈牙利语"},
            {"is", "Icelandic", "Íslenska", "冰岛语"},
            {"id", "Indonesian", "Bahasa Indonesia", "印尼语"},
            {"it", "Italian", "Italiano", "意大利语"},
            {"ja", "Japanese", "日本語", "日语"},
            {"kn", "Kannada", "ಕನ್ನಡ", "卡纳达语"},
            {"kk", "Kazakh", "Қазақ тілі", "哈萨克语"},
            {"km", "Khmer", "ខ្មែរ", "高棉语"},
            {"ko", "Korean", "한국어", "韩语"},
            {"lv", "Latvian", "Latviešu", "拉脱维亚语"},
            {"lt", "Lithuanian", "Lietuvių", "立陶宛语"},
            {"ms", "Malay", "Bahasa Melayu", "马来语"},
            {"ml", "Malayalam", "മലയാളം", "马拉雅拉姆语"},
            {"mr", "Marathi", "मराठी", "马拉地语"},
            {"mn", "Mongolian", "Монгол", "蒙古语"},
            {"no", "Norwegian", "Norsk", "挪威语"},
            {"fa", "Persian", "فارسی", "波斯语"},
            {"pl", "Polish", "Polski", "波兰语"},
            {"pt-BR", "Portuguese (Brazil)", "Português (Brasil)", "巴西葡萄牙语"},
            {"pt-PT", "Portuguese (Portugal)", "Português (Portugal)", "葡萄牙语"},
            {"pa", "Punjabi", "ਪੰਜਾਬੀ", "旁遮普语"},
            {"ro", "Romanian", "Română", "罗马尼亚语"},
            {"ru", "Russian", "Русский", "俄语"},
            {"sr", "Serbian", "Српски", "塞尔维亚语"},
            {"sk", "Slovak", "Slovenčina", "斯洛伐克语"},
            {"sl", "Slovenian", "Slovenščina", "斯洛文尼亚语"},
            {"es", "Spanish", "Español", "西班牙语"},
            {"es-ES", "Spanish (Spain)", "Español (España)", "西班牙语（西班牙）"},
            {"sw", "Swahili", "Kiswahili", "斯瓦希里语"},
            {"sv", "Swedish", "Svenska", "瑞典语"},
            {"ta", "Tamil", "தமிழ்", "泰米尔语"},
            {"te", "Telugu", "తెలుగు", "泰卢固语"},
            {"th", "Thai", "ไทย", "泰语"},
            {"bo", "Tibetan", "བོད་ཡིག", "藏语"},
            {"tr", "Turkish", "Türkçe", "土耳其语"},
            {"uk", "Ukrainian", "Українська", "乌克兰语"},
            {"ur", "Urdu", "اردو", "乌尔都语"},
            {"ug", "Uyghur", "ئۇيغۇرچە", "维吾尔语"},
            {"vi", "Vietnamese", "Tiếng Việt", "越南语"},
            {"zu", "Zulu", "isiZulu", "祖鲁语"},
        };
        std::stable_sort(v.begin(), v.end(), [](const Language& a, const Language& b) { return a.name < b.name; });
        return v;
    }();
    return list;
}

namespace {
std::string canon(const std::string& s) {
    std::string r;
    for (char c : s) r += (c == '_') ? '-' : (char)std::tolower((unsigned char)c);
    return r;
}
}  // namespace

const Language* find_language(const std::string& code) {
    const std::string want = canon(code);
    for (const auto& l : languages())
        if (canon(l.code) == want) return &l;
    // Common aliases
    if (want == "pt") return find_language("pt-BR");
    if (want == "tl") return find_language("fil");
    if (want == "nb" || want == "nn") return find_language("no");
    if (want == "zh-cn" || want == "zh-hans") return find_language("zh");
    if (want == "zh-tw" || want == "zh-hk") return find_language("zh-Hant");
    return nullptr;
}

std::string language_label(const std::string& code) {
    const Language* l = find_language(code);
    return l ? l->name + " (" + l->code + ")" : code;
}

}  // namespace tl
