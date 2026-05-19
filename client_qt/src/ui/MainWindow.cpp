#include "liteim_client/ui/MainWindow.hpp"

#include "liteim_client/ui/ChatPage.hpp"
#include "liteim_client/ui/ConversationListWidget.hpp"
#include "liteim_client/ui/SideBar.hpp"

#include <QSplitter>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

namespace liteim::client {

namespace {
// 根据 section_id 返回对应的标题文本
QString sectionTitle(const QString& section_id) {
    if (section_id == QStringLiteral("contacts")) {
        return QStringLiteral("Contacts");
    }
    if (section_id == QStringLiteral("groups")) {
        return QStringLiteral("Groups");
    }
    if (section_id == QStringLiteral("settings")) {
        return QStringLiteral("Settings");
    }
    return QStringLiteral("Messages");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("LiteIM");
    resize(1080, 720);
    setMinimumSize(900, 600);
    buildUi();
}

// 创建三栏布局
void MainWindow::buildUi() {
    // 创建一个普通 QWidget 作为 QMainWindow 的中央区域
    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("mainWindowRoot"));

    // 创建根布局
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    //创建 QSplitter横向分割器
    splitter_ = new QSplitter(Qt::Horizontal, root);
    splitter_->setObjectName(QStringLiteral("mainSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(1);  // 设置分割线宽度为 1 像素

    // 创建三个子控件
    side_bar_ = new SideBar(splitter_);
    conversation_list_ = new ConversationListWidget(splitter_);
    chat_page_ = new ChatPage(splitter_);

    // 把三个子控件添加到分割器中
    splitter_->addWidget(side_bar_);
    splitter_->addWidget(conversation_list_);
    splitter_->addWidget(chat_page_);
    // 设置三栏拉伸比例
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 0);
    splitter_->setStretchFactor(2, 1);
    // 设置初始三栏宽度
    splitter_->setSizes({88, 320, 672});

    // 放进 root 布局
    layout->addWidget(splitter_);
    // 再把 root 设置成主窗口中央区域
    setCentralWidget(root);

    // 建立SideBar信号连接
    connect(side_bar_, &SideBar::sectionSelected, this, &MainWindow::switchSection);
    // 初始化默认页面
    switchSection(QStringLiteral("messages"));
}

// 一次点击会同时更新三个区域
void MainWindow::switchSection(const QString& section_id) {
    side_bar_->setActiveSection(section_id);
    conversation_list_->setSection(section_id);
    chat_page_->setActiveSection(sectionTitle(section_id));
}

}  // namespace liteim::client
