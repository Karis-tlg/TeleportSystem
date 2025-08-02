#include "ltps/modules/tpa/TpaModule.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/modules/tpa/TpaCommand.h"
#include "ltps/modules/tpa/TpaRequest.h"
#include "ltps/modules/tpa/event/TpaEvents.h"
#include "ltps/utils/McUtils.h"
#include <algorithm>

namespace ltps::tpa {
TpaModule::TpaModule() = default;
std::vector<std::string> TpaModule::getDependencies() const { return {}; }

bool TpaModule::isLoadable() const { return getConfig().modules.tpa.enable; }

bool TpaModule::init() {
    if (!mTpaRequestPool) {
        mTpaRequestPool = std::make_unique<TpaRequestPool>(getThreadPool());
    }
    return true;
}

bool TpaModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    mListeners.emplace_back(bus.emplaceListener<CreateTpaRequestEvent>(
        [this, &bus](CreateTpaRequestEvent& ev) {
            auto before = CreatingTpaRequestEvent(ev);
            bus.publish(before);

            if (before.isCancelled()) {
                return;
            }

            auto ptr = getRequestPool().createRequest(ev.getSender(), ev.getReceiver(), ev.getType());

            ev.invokeCallback(ptr);

            bus.publish(CreatedTpaRequestEvent(ptr));
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<CreatingTpaRequestEvent>(
        [this](CreatingTpaRequestEvent& ev) {
            auto& sender = ev.getSender();
            auto localeCode = sender.getLocaleCode();

            // Kiểm tra chiều không gian
            if (std::find(
                    getConfig().modules.tpa.disallowedDimensions.begin(),
                    getConfig().modules.tpa.disallowedDimensions.end(),
                    sender.getDimensionId()
                )
                != getConfig().modules.tpa.disallowedDimensions.end()) {
                mc_utils::sendText<mc_utils::Error>(sender, "Chức năng này không khả dụng trong chiều không gian hiện tại"_trl(localeCode));
                ev.cancel();
                return;
            }

            // Kiểm tra thời gian hồi chiêu TPA
            if (this->mCooldown.isCooldown(sender.getRealName())) {
                mc_utils::sendText<mc_utils::Error>(
                    sender,
                    "Yêu cầu TPA đang hồi chiêu, còn lại {0}"_trl(
                        localeCode,
                        this->mCooldown.getCooldownString(sender.getRealName())
                    )
                );
                ev.cancel();
                return;
            }
            this->mCooldown.setCooldown(sender.getRealName(), getConfig().modules.tpa.cooldownTime);

            // Tính phí
            PriceCalculate cl(getConfig().modules.tpa.createRequestCalculate);
            auto clValue = cl.eval();
            if (!clValue.has_value()) {
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "Đã xảy ra lỗi khi tính phí TPA, vui lòng kiểm tra cấu hình.\n{}",
                    clValue.error()
                );
                mc_utils::sendText<mc_utils::Error>(sender, "Module TPA gặp lỗi, vui lòng liên hệ quản trị viên"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto price = static_cast<llong>(*clValue);

            auto economy = EconomySystemManager::getInstance().getEconomySystem();
            if (!economy->has(sender, price)) {
                economy->sendNotEnoughMoneyMessage(sender, price, localeCode);
                ev.cancel();
                return;
            }

            if (!economy->reduce(sender, price)) {
                mc_utils::sendText<mc_utils::Error>(sender, "Trừ phí TPA thất bại, vui lòng liên hệ quản trị viên"_trl(localeCode));
                ev.cancel();
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<CreatedTpaRequestEvent>(
        [](CreatedTpaRequestEvent& ev) {
            auto request  = ev.getRequest();
            auto sender   = request->getSender();
            auto receiver = request->getReceiver();
            auto type     = TpaRequest::getTypeString(request->getType());

            mc_utils::sendText(
                *sender,
                "Đã gửi yêu cầu '{1}' tới '{0}'"_trl(sender->getLocaleCode(), receiver->getRealName(), type)
            );
            mc_utils::sendText(
                *receiver,
                "Bạn đã nhận được yêu cầu '{1}' từ '{0}'"_trl(receiver->getLocaleCode(), sender->getRealName(), type)
            );
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<TpaRequestAcceptedEvent>(
        [](TpaRequestAcceptedEvent& ev) {
            auto sender   = ev.getRequest().getSender();
            auto receiver = ev.getRequest().getReceiver();
            auto type     = ev.getRequest().getType();

            mc_utils::sendText(
                *sender,
                "'{0}' đã chấp nhận yêu cầu '{1}' của bạn."_trl(
                    sender->getLocaleCode(),
                    receiver->getRealName(),
                    TpaRequest::getTypeString(type)
                )
            );
            mc_utils::sendText(
                *receiver,
                "Bạn đã chấp nhận yêu cầu '{1}' từ '{0}'."_trl(
                    receiver->getLocaleCode(),
                    sender->getRealName(),
                    TpaRequest::getTypeString(type)
                )
            );
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<TpaRequestDeniedEvent>(
        [](TpaRequestDeniedEvent& ev) {
            auto sender   = ev.getRequest().getSender();
            auto receiver = ev.getRequest().getReceiver();
            auto type     = ev.getRequest().getType();

            mc_utils::sendText<mc_utils::Error>(
                *sender,
                "'{0}' đã từ chối yêu cầu '{1}' của bạn."_trl(
                    sender->getLocaleCode(),
                    receiver->getRealName(),
                    TpaRequest::getTypeString(type)
                )
            );
            mc_utils::sendText<mc_utils::Warn>(
                *receiver,
                "Bạn đã từ chối yêu cầu '{1}' từ '{0}'."_trl(
                    receiver->getLocaleCode(),
                    sender->getRealName(),
                    TpaRequest::getTypeString(type)
                )
            );
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<PlayerExecuteTpaAcceptOrDenyCommandEvent>(
        [this](PlayerExecuteTpaAcceptOrDenyCommandEvent& ev) {
            bool const isAccept   = ev.isAccept();
            auto&      receiver   = ev.getPlayer();
            auto const localeCode = receiver.getLocaleCode();

            if (receiver.isSleeping()) {
                mc_utils::sendText<mc_utils::Error>(receiver, "Bạn không thể sử dụng lệnh này khi đang ngủ"_trl(localeCode));
                return;
            }

            auto& pool    = this->getRequestPool();
            auto  senders = pool.getSenders(receiver.getUuid());

            switch (senders.size()) {
            case 0:
                mc_utils::sendText<mc_utils::Error>(receiver, "Bạn chưa nhận được bất kỳ yêu cầu TPA nào"_trl(localeCode));
                return;
            case 1: {
                auto request = pool.getRequest(senders[0], receiver.getUuid());
                if (request) {
                    isAccept ? request->accept() : request->deny();
                } else {
                    mc_utils::sendText<mc_utils::Error>(receiver, "Yêu cầu TPA không tồn tại"_trl(localeCode));
                    TeleportSystem::getInstance().getSelf().getLogger().error("Yêu cầu TPA không hợp lệ (null pointer).");
                }
                return;
            }
            default: {
                auto& infoDb = ll::service::PlayerInfo::getInstance();

                ll::form::SimpleForm fm;
                fm.setTitle("Danh sách yêu cầu TPA [{}]"_trl(localeCode, senders.size()));
                fm.setContent("Chọn một yêu cầu TPA để chấp nhận/từ chối"_trl(localeCode));

                for (auto& sender : senders) {
                    auto info = infoDb.fromUuid(sender);
                    fm.appendButton(
                        "Người gửi: {0}"_trl(localeCode, info.has_value() ? info->name : sender.asString()),
                        [&pool, sender, isAccept](Player& self) {
                            if (auto request = pool.getRequest(self.getUuid(), sender)) {
                                isAccept ? request->accept() : request->deny();
                            }
                        }
                    );
                }

                fm.sendTo(receiver);

                return;
            }
            }
        },
        ll::event::EventPriority::High
    ));

    TpaCommand::setup();

    return true;
}

bool TpaModule::disable() {
    mTpaRequestPool.reset();

    auto& bus = ll::event::EventBus::getInstance();
    for (auto& listener : mListeners) {
        bus.removeListener(listener);
    }
    mListeners.clear();

    return true;
}

Cooldown& TpaModule::getCooldown() { return mCooldown; }

TpaRequestPool&       TpaModule::getRequestPool() { return *mTpaRequestPool; }
TpaRequestPool const& TpaModule::getRequestPool() const { return *mTpaRequestPool; }

} // namespace ltps::tpa