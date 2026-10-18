#include "citysim/content/date_text.hpp"

namespace citysim::content {

std::string render_date(const Localizer& localizer, const time::FormattedDate& date) {
    std::vector<LocArg> args;
    args.reserve(date.args.size());
    for (const time::FormattedDateArg& arg : date.args) {
        args.push_back({arg.name, arg.is_key ? localizer.get(arg.value) : arg.value});
    }
    return localizer.format(date.key, args);
}

std::string format_date_text(const Localizer& localizer, const time::Date& date) {
    return render_date(localizer, time::format_date(date));
}

} // namespace citysim::content
