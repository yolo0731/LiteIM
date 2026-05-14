#include "liteim/storage/AuthDao.hpp"

#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

namespace liteim {

AuthDao::AuthDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {}

Status AuthDao::usernameExists(const std::string& username, bool& exists) {
    exists = false;

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT user_id FROM users WHERE username = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = statement.bindString(0, username);
    if (!bind_status.isOk()) {
        return bind_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }

    exists = !result.rows().empty();
    return Status::ok();
}

}  // namespace liteim
