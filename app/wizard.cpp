#include "wizard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>
#include <QWizardPage>

#include "../core/discord.hpp"
#include "../core/protection.hpp"
#include "autostart.hpp"
#include "widgets.hpp"

using namespace hunter;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

QWizardPage* makePage(const QString& title, const QString& sub) {
    auto* p = new QWizardPage;
    p->setTitle(title);
    p->setSubTitle(sub);
    return p;
}

class AccountPage : public QWizardPage {
public:
    QLineEdit* uid;
    explicit AccountPage(QLineEdit* u, QComboBox* region) : uid(u) {
        setTitle("Your Call of Duty: Mobile account");
        setSubTitle("Only needed for CODM codes. Mainline codes (BO7, MW4, Warzone…) go to your Activision account instead.");
        auto* f = new QFormLayout(this);
        f->addRow("Player UID", uid);
        f->addRow("Account region", region);
        f->addRow(label("In the game: tap your avatar → copy the UID. It's not a password — it only tells the official "
                        "redemption page where to send rewards. It stays on this PC. You can skip this and add it later.",
                        "Muted"));
    }
    bool isComplete() const override { return uid->text().isEmpty() || validUid(uid->text().toStdString()); }
};
}  // namespace

SetupWizard::SetupWizard(Engine& engine, QWidget* parent) : QWizard(parent), engine_(engine) {
    setWindowTitle("Set up COD Redemption Hunter");
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    resize(720, 520);

    auto* welcome = makePage("Welcome", "A minute of setup and the hunter watches for Call of Duty codes around the clock.");
    auto* wv = new QVBoxLayout(welcome);
    wv->addWidget(label(
        "• It reads public code pages, feeds and channels for <b>every Call of Duty</b> — CODM, BO7, MW4, Warzone, BO6 and older titles.<br>"
        "• It scores each code and tells you how likely it is to still work.<br>"
        "• When you redeem, it opens the official page with the code filled in. <b>You</b> solve the CAPTCHA and press Redeem — "
        "it never submits for you, and it limits attempts to keep your account safe.",
        "PageHint"));
    wv->addStretch(1);
    addPage(welcome);

    uid_ = new QLineEdit(q(engine.config.env("CODM_UID")));
    uid_->setPlaceholderText("digits only");
    uid_->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{0,25}"), uid_));
    region_ = new QComboBox;
    region_->addItems({"Both", "Global", "Garena"});
    int r = region_->findText(q(engine.config.env("ACCOUNT_REGION", "Both")), Qt::MatchFixedString);
    region_->setCurrentIndex(r < 0 ? 0 : r);
    auto* acc = new AccountPage(uid_, region_);
    connect(uid_, &QLineEdit::textChanged, acc, &QWizardPage::completeChanged);
    addPage(acc);

    auto* discord = makePage("Discord (optional)", "Get new codes in your Discord server as they appear.");
    auto* df = new QFormLayout(discord);
    webhook_ = new QLineEdit(q(engine.config.env("DISCORD_WEBHOOK_URL")));
    webhook_->setPlaceholderText("https://discord.com/api/webhooks/…   (leave empty to skip)");
    df->addRow("Webhook", webhook_);
    df->addRow(label("Server Settings → Integrations → Webhooks → New Webhook → Copy Webhook URL.", "Muted"));
    addPage(discord);

    auto* done = makePage("Ready", "Choose how it runs.");
    auto* dv = new QVBoxLayout(done);
    autostart_ = new QCheckBox("Start with Windows — sits in the tray and scans 24/7");
    autostart_->setChecked(true);
    scanNow_ = new QCheckBox("Start scanning now");
    scanNow_->setChecked(true);
    dv->addWidget(autostart_);
    dv->addWidget(scanNow_);
    dv->addWidget(label("You can change all of this later in Settings.", "Muted"));
    dv->addStretch(1);
    addPage(done);
}

bool SetupWizard::startScanning() const { return scanNow_->isChecked(); }

void SetupWizard::accept() {
    QString hook = webhook_->text().trimmed();
    if (!hook.isEmpty() && !validWebhook(hook.toStdString())) {
        QMessageBox::warning(this, "Discord", "That isn't a Discord webhook URL. Leave it empty to skip.");
        return;
    }
    engine_.config.writeEnv({{"CODM_UID", uid_->text().trimmed().toStdString()},
                             {"ACCOUNT_REGION", region_->currentText().toStdString()},
                             {"DISCORD_WEBHOOK_URL", hook.toStdString()}});
    if (engine_.config.redemptionUrl("Global").empty())
        engine_.config.writeEnv({{"REDEMPTION_URL_GLOBAL", kOfficialCodmRedemptionUrl}});
    if (!uid_->text().isEmpty()) engine_.protection.binding.check();
    autostart::setEnabled(autostart_->isChecked());
    Json cfg = engine_.config.snapshot();
    cfg["runtime"]["start_monitoring_on_launch"] = scanNow_->isChecked();
    engine_.config.save(cfg);
    engine_.store.setState("setup_complete", "1");
    engine_.store.addAudit("SETUP_COMPLETED", uid_->text().isEmpty() ? "no UID" : maskUid(uid_->text().toStdString()));
    QWizard::accept();
}
