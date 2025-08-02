#include "ltps/modules/tpa/TpaRequest.h"
#include "../setting/SettingStorage.h"
#include "fmt/core.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/i18n/I18n.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/EconomySystem.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "ltps/utils/McUtils.h"
#include "ltps/utils/TimeUtils.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/actor/player/Player.h"
#include <chrono>


namespace ltps::tpa {

TpaRequest::TpaRequest(Player& sender, Player& receiver, Type type)
: mSender(sender.getWeakEntity()),
  mReceiver(receiver.getWeakEntity()),
  mType(type),
  mState(State::Available),
  mCreationTime(time_utils::now()) {}

TpaRequest::~TpaRequest() = default;

Player* TpaRequest::getSender() const { return mSender.tryUnwrap<Player>().as_ptr(); }
Player* TpaRequest::getReceiver() const { return mReceiver.tryUnwrap<Player>().as_ptr(); }

TpaRequest::Type  TpaRequest::getType() const { return mType; }
TpaRequest::State TpaRequest::getState() const { return mState; }

TpaRequest::Time const& TpaRequest::getCreationTime() const { return mCreationTime; }

std::chrono::seconds TpaRequest::getRemainingTime() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        mCreationTime + std::chrono::seconds(getConfig().modules.tpa.expirationTime) - now
    );
}

std::string TpaRequest::getExpirationTime() const {
    // 获取过期时间点
    auto expirationTime = mCreationTime + std::chrono::seconds(getConfig().modules.tpa.expirationTime);
    return time_utils::timeToString(expirationTime);
}

void TpaRequest::setState(State state) { mState = state; }

bool TpaRequest::isExpired() const {
    auto now = std::chrono::system_clock::now();
    return now >= (mCreationTime + std::chrono::seconds(getConfig().modules.tpa.expirationTime));
}

bool TpaRequest::isAvailable() const { return mState == State::Available; }

void TpaRequest::forceUpdateState() {
    if (mState != State::Available) {
        return; // 请求有效，无需更新
    }

    if (auto sender = getSender(); !sender) {
        setState(State::SenderOffline);
    }
    if (auto receiver = getReceiver(); !receiver) {
        setState(State::ReceiverOffline);
    }
    if (isExpired()) {
        setState(State::Expired);
    }
}

void TpaRequest::accept() {
    forceUpdateState();
    if (!isAvailable()) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    TpaRequestAcceptingEvent event(*this);
    bus.publish(event);
    if (event.isCancelled()) {
        return;
    }


    auto sender   = getSender();
    auto receiver = getReceiver();

    switch (mType) {
    case Type::To: {
        sender->teleport(receiver->getPosition(), receiver->getDimensionId(), mc_utils::getRotation(*sender));
        break;
    }
    case Type::Here: {
        receiver->teleport(sender->getPosition(), sender->getDimensionId(), mc_utils::getRotation(*receiver));
        break;
    }
    }

    setState(State::Accepted);

    bus.publish(TpaRequestAcceptedEvent(*this));
}

void TpaRequest::deny() {
    forceUpdateState();
    if (!isAvailable()) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    TpaRequestDenyingEvent event(*this);
    bus.publish(event);
    if (event.isCancelled()) {
        return;
    }

    setState(State::Denied);

    bus.publish(TpaRequestDeniedEvent(*this));
}

void TpaRequest::sendFormToReceiver() {
    forceUpdateState();
    if (!isAvailable()) {
        return;
    }
    auto receiver = getReceiver();
    auto sender   = getSender();

    auto& settingStorage     = *TeleportSystem::getInstance().getStorageManager().getStorage<setting::SettingStorage>();
    auto  receiverSettings   = settingStorage.getSettingData(receiver->getRealName()).value();
    auto  receiverLocaleCode = receiver->getLocaleCode();

    if (!receiverSettings.tpaPopup) {
        return; // Người chơi không chấp nhận popup TPA
    }

    ll::form::SimpleForm form;
    form.setTitle("Yêu cầu TPA"_trl(receiverLocaleCode));

    std::string desc = mType == Type::To
                         ? "'{0}' muốn dịch chuyển đến vị trí của bạn"_trl(receiverLocaleCode, sender->getRealName())
                         : "'{0}' muốn dịch chuyển bạn đến vị trí của họ"_trl(receiverLocaleCode, sender->getRealName());
    form.setContent(desc);
    form.appendButton("Chấp nhận"_trl(receiverLocaleCode), "textures/ui/realms_green_check", "path", [this](Player&) {
        accept();
    });
    form.appendButton("Từ chối"_trl(receiverLocaleCode), "textures/ui/realms_red_x", "path", [this](Player&) { deny(); });
    form.appendButton(
        "Bỏ qua\nHết hạn sau: {0}"_trl(receiverLocaleCode, getExpirationTime()),
        "textures/ui/backup_replace",
        "path"
    );

    form.sendTo(*receiver);
}


std::string TpaRequest::getStateDescription(State state, std::string const& localeCode) {
    switch (state) {
    case State::Available:
        return "Yêu cầu còn hiệu lực"_trl(localeCode);
    case State::Accepted:
        return "Yêu cầu đã được chấp nhận"_trl(localeCode);
    case State::Denied:
        return "Yêu cầu đã bị từ chối"_trl(localeCode);
    case State::Expired:
        return "Yêu cầu đã hết hạn"_trl(localeCode);
    case State::SenderOffline:
        return "Người gửi đã thoát"_trl(localeCode);
    case State::ReceiverOffline:
        return "Người nhận đã thoát"_trl(localeCode);
    }
    return "Trạng thái không xác định"_trl(localeCode);
}

std::string TpaRequest::getTypeString(Type type) {
    switch (type) {
    case Type::To:
        return "tpa";
    case Type::Here:
        return "tpahere";
    }
    return "unknown";
}


} // namespace ltps::tpa