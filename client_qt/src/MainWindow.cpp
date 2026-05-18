#include "liteim_client/MainWindow.hpp"

#include "liteim_client/ChatPage.hpp"
#include "liteim_client/ConversationListWidget.hpp"
#include "liteim_client/SideBar.hpp"

#include <QSplitter>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace liteim::client {

namespace {

QString sectionTitle(const QString& section_id) {
    if (section_id == QStringLiteral("contacts")) {
        return QStringLiteral("Contacts");
    }
    if (section_id == QStringLiteral("groups")) {
        return QStringLiteral("Groups");
    }
    if (section_id == QStringLiteral("agent")) {
        return QStringLiteral("Agent");
    }
    if (section_id == QStringLiteral("settings")) {
        return QStringLiteral("Settings");
    }
    return QStringLiteral("Messages");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("LiteIM");
    resize(1080, 720);
    setMinimumSize(900, 600);
    buildUi();
}

void MainWindow::buildUi() {
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("mainWindowRoot"));

    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    splitter_ = new QSplitter(Qt::Horizontal, root);
    splitter_->setObjectName(QStringLiteral("mainSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(1);

    side_bar_ = new SideBar(splitter_);
    conversation_list_ = new ConversationListWidget(splitter_);
    chat_page_ = new ChatPage(splitter_);

    splitter_->addWidget(side_bar_);
    splitter_->addWidget(conversation_list_);
    splitter_->addWidget(chat_page_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 0);
    splitter_->setStretchFactor(2, 1);
    splitter_->setSizes({88, 320, 672});

    layout->addWidget(splitter_);
    setCentralWidget(root);

    connect(side_bar_, &SideBar::sectionSelected, this, &MainWindow::switchSection);
    switchSection(QStringLiteral("messages"));
}

void MainWindow::switchSection(const QString& section_id) {
    side_bar_->setActiveSection(section_id);
    conversation_list_->setSection(section_id);
    chat_page_->setActiveSection(sectionTitle(section_id));
}

}  // namespace liteim::client
