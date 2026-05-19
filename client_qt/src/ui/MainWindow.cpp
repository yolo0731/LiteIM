#include "liteim_client/ui/MainWindow.hpp"

#include "liteim_client/app/ClientRuntime.hpp"
#include "liteim_client/chat/ChatController.hpp"
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

QString avatarTextFor(const QString& title) {
    return title.isEmpty() ? QStringLiteral("?") : title.left(1);
}

QString fallbackGroupTitle(const QString& conversation_id) {
    const auto prefix = QStringLiteral("group:");
    if (conversation_id.startsWith(prefix)) {
        return QStringLiteral("Group ") + conversation_id.mid(prefix.size());
    }
    return QStringLiteral("Group");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    owned_runtime_ = new ClientRuntime(this);
    runtime_ = owned_runtime_;
    initializeWindow();
}

MainWindow::MainWindow(ClientRuntime& runtime, QWidget* parent)
    : QMainWindow(parent), runtime_(&runtime) {
    initializeWindow();
}

void MainWindow::initializeWindow() {
    setWindowTitle("LiteIM");
    resize(1080, 720);
    setMinimumSize(900, 600);

    chat_controller_ = new ChatController(*runtime_, this);
    buildUi();
    connectChatFlow();
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

void MainWindow::connectChatFlow() {
    connect(conversation_list_, &ConversationListWidget::conversationActivated, this,
            &MainWindow::openConversation);
    connect(chat_page_, &ChatPage::historyRequested, this, &MainWindow::requestHistory);
    connect(chat_page_, &ChatPage::sendMessageRequested, this, &MainWindow::sendMessage);
    connect(chat_controller_, &ChatController::messageReceived, this,
            &MainWindow::handleIncomingMessage);
    connect(chat_controller_, &ChatController::messageDelivered, this,
            &MainWindow::handleDeliveredMessage);
    connect(chat_controller_, &ChatController::historyLoaded, this,
            &MainWindow::applyHistoryMessages);
}

void MainWindow::openConversation(const QString& conversation_id,
                                  const QString& title,
                                  ConversationKind kind,
                                  std::uint64_t target_id) {
    active_conversation_id_ = conversation_id;
    active_conversation_title_ = title;
    active_conversation_kind_ = kind;
    active_target_id_ = target_id;
    active_wire_conversation_id_ =
        kind == ConversationKind::Group
            ? target_id
            : ChatController::privateConversationId(runtime_->session().userId(), target_id);

    conversation_list_->markConversationRead(conversation_id);
    chat_page_->openConversation(conversation_id, title, kind);
}

void MainWindow::requestHistory(const QString& conversation_id, quint64 before_message_id) {
    if (conversation_id != active_conversation_id_ || active_wire_conversation_id_ == 0) {
        return;
    }

    const auto status = chat_controller_->requestHistory(active_conversation_kind_,
                                                         active_wire_conversation_id_,
                                                         before_message_id);
    if (!status.isOk()) {
        chat_controller_->reportFailure(QString::fromStdString(status.message()));
    }
}

void MainWindow::sendMessage(const QString& conversation_id, const QString& text) {
    if (conversation_id != active_conversation_id_ || active_target_id_ == 0) {
        return;
    }

    const auto status = active_conversation_kind_ == ConversationKind::Group
                            ? chat_controller_->sendGroupMessage(active_target_id_, text)
                            : chat_controller_->sendPrivateMessage(active_target_id_, text);
    if (!status.isOk()) {
        chat_controller_->reportFailure(QString::fromStdString(status.message()));
    }
}

void MainWindow::handleIncomingMessage(const ChatMessage& message) {
    ConversationUpdate update;
    update.conversation_id = message.conversation_id;
    update.kind = message.conversation_id.startsWith(QStringLiteral("group:"))
                      ? ConversationKind::Group
                      : ConversationKind::Private;
    update.title = update.kind == ConversationKind::Group
                       ? (message.conversation_id == active_conversation_id_
                              ? active_conversation_title_
                              : fallbackGroupTitle(message.conversation_id))
                       : message.sender_name;
    update.last_message = message.text;
    update.timestamp = message.sent_at.toString(QStringLiteral("HH:mm"));
    update.avatar_text = avatarTextFor(update.title);
    conversation_list_->applyIncomingMessage(update, active_conversation_id_);

    if (message.conversation_id == active_conversation_id_) {
        chat_page_->appendMessage(message);
    }
}

void MainWindow::handleDeliveredMessage(const ChatMessage& message) {
    if (message.conversation_id != active_conversation_id_) {
        return;
    }
    chat_page_->updateMessageStatus(0, MessageSendStatus::Succeeded);
}

void MainWindow::applyHistoryMessages(const QVector<ChatMessage>& messages) {
    chat_page_->setMessages(messages);
}

}  // namespace liteim::client
