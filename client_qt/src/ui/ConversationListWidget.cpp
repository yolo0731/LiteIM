#include "liteim_client/ui/ConversationListWidget.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace liteim::client {

namespace {

// 自定义 Messages 会话行怎么画
class ConversationItemDelegate final : public QStyledItemDelegate {
public:
    explicit ConversationItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto base = QStyledItemDelegate::sizeHint(option, index);
        return {base.width(), 72};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto row_rect = option.rect.adjusted(0, 4, -2, -4);
        const auto selected = (option.state & QStyle::State_Selected) != 0;
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? QColor(QStringLiteral("#ead8bd")) : QColor(Qt::transparent));
        painter->drawRoundedRect(row_rect, 10, 10);

        const auto kind =
            static_cast<ConversationKind>(index.data(ConversationModel::KindRole).toInt());
        const auto title = index.data(ConversationModel::TitleRole).toString();
        const auto last_message = index.data(ConversationModel::LastMessageRole).toString();
        const auto timestamp = index.data(ConversationModel::TimestampRole).toString();
        const auto avatar_text = index.data(ConversationModel::AvatarTextRole).toString();
        const auto unread_count = index.data(ConversationModel::UnreadCountRole).toInt();

        const QRect avatar_rect(row_rect.left() + 10, row_rect.top() + 13, 38, 38);
        painter->setBrush(kind == ConversationKind::Group ? QColor(QStringLiteral("#28784f"))
                                                          : QColor(QStringLiteral("#f0c46a")));
        painter->drawEllipse(avatar_rect);

        QFont avatar_font = option.font;
        avatar_font.setBold(true);
        painter->setFont(avatar_font);
        painter->setPen(QColor(QStringLiteral("#17201a")));
        painter->drawText(avatar_rect, Qt::AlignCenter, avatar_text.left(2));

        const auto text_left = avatar_rect.right() + 12;
        const auto text_right = row_rect.right() - 12;
        const QRect title_rect(text_left, row_rect.top() + 11, text_right - text_left, 22);
        const QRect message_rect(text_left, row_rect.top() + 36, text_right - text_left, 20);

        QFont title_font = option.font;
        title_font.setBold(true);
        painter->setFont(title_font);
        painter->setPen(QColor(QStringLiteral("#17201a")));
        const QFontMetrics title_metrics(title_font);
        painter->drawText(title_rect, Qt::AlignLeft | Qt::AlignVCenter,
                          title_metrics.elidedText(title, Qt::ElideRight, title_rect.width() - 58));

        painter->setFont(option.font);
        painter->setPen(QColor(QStringLiteral("#7b6f60")));
        const QFontMetrics text_metrics(option.font);
        painter->drawText(
            message_rect, Qt::AlignLeft | Qt::AlignVCenter,
            text_metrics.elidedText(last_message, Qt::ElideRight, message_rect.width() - 42));
        painter->drawText(title_rect, Qt::AlignRight | Qt::AlignVCenter, timestamp);

        if (unread_count > 0) {
            const auto unread_text =
                unread_count > 99 ? QStringLiteral("99+") : QString::number(unread_count);
            QFont badge_font = option.font;
            badge_font.setPointSize(std::max(8, badge_font.pointSize() - 2));
            badge_font.setBold(true);
            const QFontMetrics badge_metrics(badge_font);
            const auto badge_width =
                std::max(20, badge_metrics.horizontalAdvance(unread_text) + 10);
            const QRect badge_rect(row_rect.right() - badge_width - 10, row_rect.bottom() - 28,
                                   badge_width, 20);
            painter->setFont(badge_font);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(QStringLiteral("#e95454")));
            painter->drawRoundedRect(badge_rect, 10, 10);
            painter->setPen(Qt::white);
            painter->drawText(badge_rect, Qt::AlignCenter, unread_text);
        }

        painter->restore();
    }
};

QVector<ConversationItem> defaultConversations() {
    return {
        {QStringLiteral("private:1002"), ConversationKind::Private, QStringLiteral("Bob"),
         QStringLiteral("See you at 18:30"), QStringLiteral("09:41"), QStringLiteral("B"), 2,
         1002},
        {QStringLiteral("group:2001"), ConversationKind::Group, QStringLiteral("LiteIM Dev Group"),
         QStringLiteral("Alice: Step49 UI model ready"), QStringLiteral("Yesterday"),
         QStringLiteral("G"), 0, 2001},
    };
}

QVector<ContactListItem> defaultContacts() {
    return {
        {QStringLiteral("Alice"), QStringLiteral("Online"), QStringLiteral("A"), true, 0, 1003,
         QStringLiteral("private:1003"), ConversationKind::Private},
        {QStringLiteral("Bob"), QStringLiteral("Offline"), QStringLiteral("B"), false, 0, 1002,
         QStringLiteral("private:1002"), ConversationKind::Private},
        {QStringLiteral("PersonaAgent"), QStringLiteral("Future BotClient account"),
         QStringLiteral("P"), false, 0, 3001, QStringLiteral("private:3001"),
         ConversationKind::Private},
    };
}

QVector<ContactListItem> defaultGroups() {
    return {
        {QStringLiteral("LiteIM Dev Group"), QStringLiteral("3 members"), QStringLiteral("G"),
         false, 0, 2001, QStringLiteral("group:2001"), ConversationKind::Group},
        {QStringLiteral("Backend Notes"), QStringLiteral("2 members"), QStringLiteral("B"), false,
         0, 2002, QStringLiteral("group:2002"), ConversationKind::Group},
    };
}

}  // namespace

ConversationListWidget::ConversationListWidget(QWidget* parent)
    : QWidget(parent), conversation_model_(this) {
    setObjectName(QStringLiteral("conversationListWidget"));
    setMinimumWidth(280);
    setMaximumWidth(360);

    // 垂直布局：标题 + 搜索框 + 内容区
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    // 标题
    title_label_ = new QLabel(this);
    title_label_->setObjectName(QStringLiteral("conversationSectionTitle"));

    // 搜索框
    auto* search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("conversationSearchEdit"));
    search->setPlaceholderText(QStringLiteral("Search"));
    search->setEnabled(false);

    // 会话列表
    conversation_view_ = new QListView(this);
    conversation_view_->setObjectName(QStringLiteral("conversationListItems"));
    conversation_view_->setFrameShape(QFrame::NoFrame);
    conversation_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    conversation_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    conversation_view_->setUniformItemSizes(true);
    conversation_view_->setItemDelegate(new ConversationItemDelegate(conversation_view_));
    conversation_view_->setModel(&conversation_model_);
    connect(conversation_view_, &QListView::clicked, this,
            &ConversationListWidget::handleConversationClicked);

    // 联系人和群组列表复用 ContactListWidget
    contact_list_ = new ContactListWidget(QStringLiteral("contactListItems"), this);
    group_list_ = new ContactListWidget(QStringLiteral("groupListItems"), this);
    connect(contact_list_, &ContactListWidget::itemActivated, this,
            &ConversationListWidget::conversationActivated);
    connect(group_list_, &ContactListWidget::itemActivated, this,
            &ConversationListWidget::conversationActivated);

    // 设置页
    settings_list_ = new QListWidget(this);
    settings_list_->setObjectName(QStringLiteral("settingsListItems"));
    settings_list_->setFrameShape(QFrame::NoFrame);
    settings_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    settings_list_->addItem(QStringLiteral("Settings placeholder"));

    // 内容区是多页面容器，一次只显示其中一个页面
    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("conversationListStack"));
    stack_->addWidget(conversation_view_);
    stack_->addWidget(contact_list_);
    stack_->addWidget(group_list_);
    stack_->addWidget(settings_list_);

    // 把标题、搜索框和内容区添加到布局
    layout->addWidget(title_label_);
    layout->addWidget(search);
    layout->addWidget(stack_, 1);

    seedDemoData();
    setSection(QStringLiteral("messages"));
}

// 切换中间栏页面
void ConversationListWidget::setSection(const QString& section_id) {
    if (section_id == QStringLiteral("contacts")) {
        title_label_->setText(QStringLiteral("Contacts"));
        stack_->setCurrentWidget(contact_list_);
        return;
    }
    if (section_id == QStringLiteral("groups")) {
        title_label_->setText(QStringLiteral("Groups"));
        stack_->setCurrentWidget(group_list_);
        return;
    }
    if (section_id == QStringLiteral("settings")) {
        title_label_->setText(QStringLiteral("Settings"));
        stack_->setCurrentWidget(settings_list_);
        return;
    }

    title_label_->setText(QStringLiteral("Messages"));
    stack_->setCurrentWidget(conversation_view_);
}

ConversationModel* ConversationListWidget::conversationModel() noexcept {
    return &conversation_model_;
}

// 收到消息时，转交给 ConversationModel 更新
void ConversationListWidget::applyIncomingMessage(const ConversationUpdate& update,
                                                  const QString& active_conversation_id) {
    conversation_model_.applyIncomingMessage(update, active_conversation_id);
}

void ConversationListWidget::markConversationRead(const QString& conversation_id) {
    conversation_model_.markConversationRead(conversation_id);
}

void ConversationListWidget::handleConversationClicked(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }

    emit conversationActivated(
        conversation_model_.data(index, ConversationModel::ConversationIdRole).toString(),
        conversation_model_.data(index, ConversationModel::TitleRole).toString(),
        static_cast<ConversationKind>(
            conversation_model_.data(index, ConversationModel::KindRole).toInt()),
        conversation_model_.data(index, ConversationModel::TargetIdRole).toULongLong());
}

// 给三个页面填默认数据
void ConversationListWidget::seedDemoData() {
    conversation_model_.setConversations(defaultConversations());
    contact_list_->setItems(defaultContacts());
    group_list_->setItems(defaultGroups());
}

}  // namespace liteim::client
