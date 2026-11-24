#include "codesmodel.hpp"
#include "uitranslate.hpp"

#include <QDateTime>
#include <QFont>

#include "../core/checker.hpp"
#include "../core/games.hpp"
#include "theme.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

QString ago(const std::string& iso) {
    QDateTime t = QDateTime::fromString(q(iso), Qt::ISODate);
    if (!t.isValid()) return "—";
    qint64 mins = t.secsTo(QDateTime::currentDateTimeUtc()) / 60;
    if (mins < 60) return QStringLiteral("%1 min ago").arg(qMax<qint64>(1, mins));
    if (mins < 48 * 60) return QStringLiteral("%1 h ago").arg(mins / 60);
    return QStringLiteral("%1 days ago").arg(mins / 1440);
}

QString prettyVerdict(const CodeRow& r) {
    if (r.checkVerdict.empty()) return q(r.status).replace('_', ' ').toLower();
    QString v = q(r.checkVerdict);
    if (v == "LIKELY WORKS") return "✅ Likely works";
    if (v == "UNCERTAIN") return "❔ Uncertain";
    if (v == "REDEEMED BY YOU") return "🎁 Redeemed";
    if (v == "CLAIMED BY YOU") return "🎁 Claimed";
    if (v.startsWith("DEAD")) return "❌ " + v.mid(7).toLower();
    if (v.startsWith("LIKELY DEAD")) return "⚠ " + v.mid(14).toLower();
    return v.toLower();
}
}  // namespace

void CodesModel::reload(const std::string& view, const std::string& search, const std::string& game) {
    beginResetModel();
    rows_ = store_.codesView(view, search, game);
    rewards_.clear();
    rewards_.reserve(rows_.size());
    for (const auto& r : rows_) rewards_.push_back(q(rewardFrom(r.snippet, r.code)));
    endResetModel();
}

int CodesModel::rowOfId(long long id) const {
    for (size_t i = 0; i < rows_.size(); ++i) if (rows_[i].id == id) return int(i);
    return -1;
}

QVariant CodesModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= int(rows_.size())) return {};
    const CodeRow& r = rows_[size_t(idx.row())];
    const int c = idx.column();
    if (role == IdRole) return QVariant::fromValue<qlonglong>(r.id);
    if (role == Qt::DisplayRole || role == SortRole) {
        switch (c) {
            case Code: return q(r.code) + (r.fresh && role == Qt::DisplayRole ? QStringLiteral("  •NEW") : QString());
            case Game: return q(gameById(r.game).label());
            case Reward: return rewards_[size_t(idx.row())].isEmpty() ? QStringLiteral("—") : rewards_[size_t(idx.row())];
            case Verdict:
                return role == SortRole ? QVariant(verdictRank(r.checkVerdict)) : QVariant(prettyVerdict(r));
            case Chance:
                if (role == SortRole) return r.checkScore;
                return r.checkScore >= 0 ? QStringLiteral("%1%").arg(r.checkScore) : QStringLiteral("—");
            case Where:
                return r.game == "codm" ? q(r.region) : (r.platform.empty() ? QStringLiteral("All platforms") : q(r.platform));
            case Sites: return role == SortRole ? QVariant(r.sourceCount) : QVariant(QString::number(r.sourceCount));
            case Seen: return role == SortRole ? QVariant(q(r.firstSeen)) : QVariant(ago(r.firstSeen));
        }
    }
    if (role == Qt::ForegroundRole) {
        if (c == Verdict) return r.checkVerdict.empty() ? theme::statusColor(q(r.status)) : theme::verdictColor(q(r.checkVerdict));
        if (c == Chance && r.checkScore >= 0)
            return r.checkScore >= 65 ? theme::current().green : r.checkScore >= 35 ? theme::current().yellow : theme::current().red;
        if (c == Code && r.fresh) return theme::current().accent;
        if (c == Seen || c == Sites || c == Where) return theme::current().muted;
    }
    if (role == Qt::FontRole && c == Code) {
        QFont f(theme::monoFont());
        f.setBold(true);
        return f;
    }
    if (role == Qt::TextAlignmentRole && (c == Chance || c == Sites)) return int(Qt::AlignCenter);
    if (role == Qt::ToolTipRole) {
        if (c == Verdict || c == Chance) return q(r.checkNotes);
        if (c == Reward) return q(r.snippet);
    }
    return {};
}

QVariant CodesModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* names[] = {"CODE", "GAME", "REWARD", "STATUS", "CHANCE", "REGION / PLATFORM", "SITES", "FIRST SEEN"};
    return ui18n::tr(names[section]);
}
