#include "taiyin/ziwei/ziweicore.h"
#include <iostream>

#ifndef TAIYIN_ZIWEI_RULES_FILE
#define TAIYIN_ZIWEI_RULES_FILE "ziwei_astrology/rules/default.toml"
#endif

int main() {
    using namespace taiyin;
    using namespace taiyin::ziwei;
    try {
        ZiweiDataCatalog catalog(TAIYIN_ZIWEI_RULES_FILE);
        const ZiweiContext ctx = catalog.create_context();
        const CompiledRules& rules = ctx.compiled_tables();

        // No astronomy runtime, ephemeris data, or birth date is required.
        CastingChart reported, random, edited, shifted, restored;
        if (casting_chart_from_number("123456", Gender::Male, ZiweiChartMode::TianPan,
                rules, &reported) != TAIYIN_STATUS_OK
            || random_casting_chart(Gender::Male, ZiweiChartMode::TianPan,
                rules, &random) != TAIYIN_STATUS_OK) return 1;

        std::cout << "number-v1 index: " << reported.index << '\n';
        std::cout << "random index-v1 (save to replay): " << random.index << '\n';
        PlacementPatch patch;
        patch.month = 3;
        patch.update_bureau = 1;
        if (modify_casting_chart(reported, patch, rules, &edited) != TAIYIN_STATUS_OK
            || shift_casting_life_palace(edited, 1, &shifted) != TAIYIN_STATUS_OK
            || reset_casting_chart(shifted, &restored) != TAIYIN_STATUS_OK) return 1;
        StarId ziwei;
        if (!ctx.star_registry().find("ziwei", &ziwei)) return 1;
        std::cout << "Ziwei physical branch: " << unsigned(shifted.plate.star_positions[ziwei]) << '\n';
        std::cout << "Current bureau: " << unsigned(bureau_number(shifted.plate.anchors.bureau)) << '\n';
        return restored.plate.star_positions == reported.plate.star_positions ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
