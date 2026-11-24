// Table model for the codes list (sortable, coloured by verdict / status).
#pragma once
#include <QAbstractTableModel>
#include <QSet>
#include <vector>

#include "../core/store.hpp"

class CodesModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Col { Code, Game, Reward, Verdict, Chance, Where, Sites, Seen, ColCount };
    enum Roles { SortRole = Qt::UserRole + 1, IdRole };

    explicit CodesModel(hunter::Store& store, QObject* parent = nullptr) : QAbstractTableModel(parent), store_(store) {}
    void reload(const std::string& view, const std::string& search, const std::string& game);
    const hunter::CodeRow* rowAt(int r) const { return r >= 0 && r < int(rows_.size()) ? &rows_[size_t(r)] : nullptr; }
    int rowOfId(long long id) const;

    int rowCount(const QModelIndex& = {}) const override { return int(rows_.size()); }
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    hunter::Store& store_;
    std::vector<hunter::CodeRow> rows_;
    std::vector<QString> rewards_;
};
