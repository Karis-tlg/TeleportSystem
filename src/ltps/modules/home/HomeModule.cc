#include "ltps/modules/home/HomeModule.h"
#include "HomeStorage.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ltps/Global.h"
#include "ltps/TeleportSystem.h"
#include "ltps/base/Config.h"
#include "ltps/common/EconomySystem.h"
#include "ltps/common/PriceCalculate.h"
#include "ltps/database/PermissionStorage.h"
#include "ltps/database/StorageManager.h"
#include "ltps/modules/home/HomeCommand.h"
#include "ltps/modules/home/event/HomeEvents.h"
#include "ltps/utils/McUtils.h"
#include "ltps/utils/StringUtils.h"


namespace ltps::home {

HomeModule::HomeModule() = default;

std::vector<std::string> HomeModule::getDependencies() const { return {}; }

bool HomeModule::isLoadable() const { return getConfig().modules.home.enable; }

bool HomeModule::init() { return true; }

bool HomeModule::enable() {
    auto& bus = ll::event::EventBus::getInstance();

    // Thêm nhà mới
    mListeners.emplace_back(bus.emplaceListener<PlayerRequestAddHomeEvent>(
        [this](PlayerRequestAddHomeEvent& ev) {
            auto&           player     = ev.getPlayer();
            auto            localeCode = player.getLocaleCode();
            RealName const& realName   = player.getRealName();

            auto home = HomeStorage::Home::make(player.getPosition(), player.getDimensionId(), ev.getName());

            auto& bus = ll::event::EventBus::getInstance();

            auto adding = HomeAddingEvent(player, home);
            bus.publish(adding);
            if (adding.isCancelled()) {
                return;
            }

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto res = storage->addHome(realName, home);
            if (!res) {
                mc_utils::sendText<mc_utils::Error>(player, "Thêm nhà thất bại"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Add home failed! player: {}, homeName: {}, error: {}",
                    realName,
                    home.name,
                    res.error()
                );
                ev.cancel();
                return;
            }

            mc_utils::sendText(player, "Thêm nhà thành công"_trl(localeCode));

            auto added = HomeAddedEvent(player, home);
            bus.publish(added);

            ev.invokeCallback(home);
        },
        ll::event::EventPriority::High
    ));

    // Kiểm tra điều kiện trước khi thêm nhà
    mListeners.emplace_back(bus.emplaceListener<HomeAddingEvent>(
        [this](HomeAddingEvent& ev) {
            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto&           player     = ev.getPlayer();
            auto            localeCode = player.getLocaleCode();
            RealName const& realName   = player.getRealName();

            auto const& dimid = ev.getHome().dimid;
            if (getConfig().modules.home.disallowedDimensions.contains(dimid)) {
                mc_utils::sendText<mc_utils::Error>(player, "Không thể tạo nhà ở thế giới này"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto const& homeName = ev.getHome().name;
            if (!string_utils::isLengthValid(homeName, getConfig().modules.home.nameLength)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Độ dài tên nhà không hợp lệ ({}/{})"_trl(
                        localeCode,
                        string_utils::length(homeName),
                        getConfig().modules.home.nameLength
                    )
                );
                ev.cancel();
                return;
            }

            if (storage->hasHome(realName, homeName)) {
                mc_utils::sendText<mc_utils::Error>(player, "Tên nhà bị trùng, hãy dùng tên khác"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto count = 0;
            if (storage->hasPlayer(realName)) {
                if (auto res = storage->getHomeCount(realName)) {
                    count = res.value();
                }
            }

            bool unLimited = false;
            if (auto pe = getStorageManager().getStorage<PermissionStorage>()) {
                unLimited = pe->hasPermission(realName, PermissionStorage::Permission::UnlimitedHome);
            }

            if (count > getConfig().modules.home.maxHome && !unLimited) {
                mc_utils::sendText<mc_utils::Error>(player, "Số lượng nhà đã vượt quá giới hạn, không thể tạo thêm"_trl(localeCode));
                ev.cancel();
                return;
            }

            PriceCalculate cl(getConfig().modules.home.createHomeCalculate);
            cl.addVariable("count", count);

            auto price = cl.eval();
            if (!price.has_value()) {
                mc_utils::sendText<mc_utils::Error>(player, "Tính giá thất bại"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Calculate price failed! player: {}, homeName: {}, count: {}, error: {}",
                    realName,
                    homeName,
                    count,
                    price.error()
                );
                ev.cancel();
                return;
            }

            auto& economy = EconomySystemManager::getInstance();
            if (!economy->reduce(player, static_cast<llong>(price.value()))) {
                // mc_utils::sendText<mc_utils::Error>(player, "Không đủ tiền, không thể tạo nhà"_trl(localeCode));
                economy->sendNotEnoughMoneyMessage(player, static_cast<llong>(price.value()), localeCode);
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));

    // Xóa nhà
    mListeners.emplace_back(bus.emplaceListener<PlayerRequestRemoveHomeEvent>(
        [this](PlayerRequestRemoveHomeEvent& ev) {
            auto& bus = ll::event::EventBus::getInstance();

            auto& player = ev.getPlayer();
            auto& name   = ev.getName();

            auto removeing = HomeRemovingEvent(player, name);
            bus.publish(removeing);
            if (removeing.isCancelled()) {
                ev.invokeCallback(false);
                ev.cancel();
                return;
            }

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto res = storage->removeHome(player.getRealName(), name);
            if (!res) {
                mc_utils::sendText<mc_utils::Error>(player, "Xóa nhà thất bại"_trl(player.getLocaleCode()));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Remove home failed! player: {}, homeName: {}, error: {}",
                    player.getRealName(),
                    name,
                    res.error()
                );
                ev.invokeCallback(false);
                ev.cancel();
                return;
            }
            mc_utils::sendText(player, "Xóa nhà {} thành công!"_trl(player.getLocaleCode(), name));

            auto removed = HomeRemovedEvent(player, name);
            bus.publish(removed);

            ev.invokeCallback(true);
        },
        ll::event::EventPriority::High
    ));

    // Dịch chuyển về nhà
    mListeners.emplace_back(bus.emplaceListener<PlayerRequestGoHomeEvent>(
        [this](PlayerRequestGoHomeEvent& ev) {
            auto& bus = ll::event::EventBus::getInstance();

            auto& player     = ev.getPlayer();
            auto  realName   = player.getRealName();
            auto  localeCode = player.getLocaleCode();
            auto& name       = ev.getName();

            auto storage = getStorage();
            if (!storage) {
                throw std::runtime_error("HomeStorage not found");
            }

            auto home = storage->getHome(realName, name);
            if (!home) {
                mc_utils::sendText<mc_utils::Error>(player, "Nhà không tồn tại"_trl(localeCode));
                ev.cancel();
                return;
            }

            auto teleporting = HomeTeleportingEvent(player, home.value());
            bus.publish(teleporting);

            if (teleporting.isCancelled()) {
                ev.cancel();
                return;
            }

            home->teleport(player);

            auto teleported = HomeTeleportedEvent(player, home.value());

            bus.publish(teleported);
            ev.invokeCallback(home.value());
        },
        ll::event::EventPriority::High
    ));

    // Kiểm tra cooldown & giá khi dịch chuyển về nhà
    mListeners.emplace_back(bus.emplaceListener<HomeTeleportingEvent>(
        [this](HomeTeleportingEvent& ev) {
            auto& player     = ev.getPlayer();
            auto  realName   = player.getRealName();
            auto  localeCode = player.getLocaleCode();

            auto& cooldown = getCooldown();

            if (cooldown.isCooldown(realName)) {
                mc_utils::sendText(
                    player,
                    "Đang trong thời gian hồi chiêu, vui lòng thử lại sau, còn lại: {}"_trl(localeCode, cooldown.getCooldownString(realName))
                );
                ev.cancel();
                return;
            }

            auto cl = PriceCalculate(getConfig().modules.home.goHomeCalculate);
            cl.addVariable("dimid", ev.getHome().dimid);
            auto price = cl.eval();

            if (!price) {
                mc_utils::sendText<mc_utils::Error>(player, "Tính giá thất bại"_trl(localeCode));
                TeleportSystem::getInstance().getSelf().getLogger().error(
                    "[HomeModule]: Calculate price failed! player: {}, homeName: {}, error: {}",
                    realName,
                    ev.getHome().name,
                    price.error()
                );
                ev.cancel();
                return;
            }

            if (const auto& economy = EconomySystemManager::getInstance();
                !economy->reduce(player, static_cast<llong>(price.value()))) {
                economy->sendNotEnoughMoneyMessage(player, static_cast<llong>(price.value()), localeCode);
                ev.cancel();
                return;
            }

            cooldown.setCooldown(realName, getConfig().modules.home.cooldownTime);
        },
        ll::event::EventPriority::High
    ));

    // Sửa nhà
    mListeners.emplace_back(bus.emplaceListener<PlayerRequestEditHomeEvent>(
        [this](PlayerRequestEditHomeEvent& ev) {
            auto& bus        = ll::event::EventBus::getInstance();
            auto& player     = ev.getPlayer();
            auto  realName   = player.getRealName();
            auto  localeCode = player.getLocaleCode();

            auto&      name    = ev.getName();
            auto const type    = ev.getType();
            auto const newPos  = ev.getNewPosition();
            auto const newName = ev.getNewName();

            auto storage = this->getStorage();
            auto home    = storage->getHome(realName, name);
            if (!home) {
                mc_utils::sendText<mc_utils::Error>(player, "Nhà {} không tồn tại, không thể thực hiện thao tác"_trl(localeCode, name));
                ev.cancel();
                return;
            }

            auto editing = HomeEditingEvent(player, type, name, *home, newPos, newName);
            bus.publish(editing);
            if (editing.isCancelled()) {
                ev.cancel();
                return;
            }

            if (newName.has_value()) {
                home->name = *newName;
            }
            if (newPos.has_value()) {
                home->updatePosition(*newPos);
            }

            if (auto res = storage->updateHome(realName, name, *home)) {
                mc_utils::sendText(player, "Nhà {} đã được cập nhật"_trl(localeCode, name));
            } else {
                mc_utils::sendText(player, "Cập nhật nhà thất bại: {}"_trl(localeCode, res.error()));
                ev.cancel();
                return;
            }

            auto edited = HomeEditedEvent(player, type, name, *home, newPos, newName);
            bus.publish(edited);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<HomeEditingEvent>(
        [this](HomeEditingEvent& ev) {
            auto&      player     = ev.getPlayer();
            auto       localeCode = player.getLocaleCode();
            auto const newName    = ev.getNewName();

            if (newName.has_value()) {
                if (!string_utils::isLengthValid(*newName, getConfig().modules.home.nameLength)) {
                    mc_utils::sendText<mc_utils::Error>(
                        player,
                        "Độ dài tên nhà không hợp lệ ({}/{})"_trl(
                            localeCode,
                            string_utils::length(*newName),
                            getConfig().modules.home.nameLength
                        )
                    );
                    ev.cancel();
                }
            }
        },
        ll::event::EventPriority::High
    ));

    // ADMIN
    mListeners.emplace_back(bus.emplaceListener<AdminRequestGoPlayerHomeEvent>(
        [this](AdminRequestGoPlayerHomeEvent& ev) {
            auto& bus    = ll::event::EventBus::getInstance();
            auto& player = ev.getAdmin();
            auto& target = ev.getTarget();
            auto& home   = ev.getHome();

            auto ing = AdminTeleportingPlayerHomeEvent{player, target, home};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            home.teleport(player);

            auto ed = AdminTeleportedPlayerHomeEvent{player, target, home};
            bus.publish(ed);
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestCreateHomeForPlayerEvent>(
        [this](AdminRequestCreateHomeForPlayerEvent& ev) {
            auto& bus        = ll::event::EventBus::getInstance();
            auto& player     = ev.getAdmin();
            auto  localeCode = player.getLocaleCode();
            auto& target     = ev.getTarget();
            auto& name       = ev.getName();
            auto& pos        = ev.getPosition();
            auto  dimid      = ev.getDimid();

            auto ing = AdminCreateingHomeForPlayerEvent{player, target, name, dimid, pos};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto home = HomeStorage::Home::make(pos, dimid, name);

            auto storage = this->getStorage();
            if (auto res = storage->addHome(target, home)) {
                mc_utils::sendText(player, "Tạo nhà {} cho người chơi {} thành công"_trl(localeCode, name, target));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Tạo nhà {} cho người chơi {} thất bại: {}"_trl(localeCode, name, target, res.error())
                );
            }
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<AdminCreateingHomeForPlayerEvent>(
        [this](AdminCreateingHomeForPlayerEvent& ev) {
            auto& player     = ev.getAdmin();
            auto  localeCode = player.getLocaleCode();
            auto  target     = ev.getTarget();
            auto& name       = ev.getName();
            auto  dimid      = ev.getDimid();

            if (getConfig().modules.home.disallowedDimensions.contains(dimid)) {
                mc_utils::sendText<mc_utils::Error>(player, "Không thể tạo nhà ở thế giới này"_trl(localeCode));
                ev.cancel();
                return;
            }

            if (!string_utils::isLengthValid(name, getConfig().modules.home.nameLength)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Độ dài tên nhà không hợp lệ ({}/{})"_trl(
                        localeCode,
                        string_utils::length(name),
                        getConfig().modules.home.nameLength
                    )
                );
                ev.cancel();
                return;
            }

            if (getStorage()->hasHome(target, name)) {
                mc_utils::sendText<mc_utils::Error>(player, "Tên nhà bị trùng, hãy dùng tên khác"_trl(localeCode));
                ev.cancel();
                return;
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestEditPlayerHomeEvent>(
        [this](AdminRequestEditPlayerHomeEvent& ev) {
            auto& bus     = ll::event::EventBus::getInstance();
            auto& player  = ev.getAdmin();
            auto& target  = ev.getTarget();
            auto& home    = ev.getHome();
            auto& newHome = ev.getNewHome();

            auto ing = AdminEditingPlayerHomeEvent{player, target, home, newHome};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto storage = this->getStorage();
            if (auto res = storage->updateHome(target, home.name, newHome)) {
                mc_utils::sendText(player, "Đã sửa nhà {} của người chơi {} thành công"_trl(player.getLocaleCode(), home.name, target));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Sửa nhà {} của người chơi {} thất bại: {}"_trl(player.getLocaleCode(), home.name, target, res.error())
                );
            }

            auto ed = AdminEditedPlayerHomeEvent{player, target, home, newHome};
            bus.publish(ed);
        },
        ll::event::EventPriority::High
    ));
    mListeners.emplace_back(bus.emplaceListener<AdminEditingPlayerHomeEvent>(
        [](AdminEditingPlayerHomeEvent& ev) {
            auto&       player     = ev.getAdmin();
            auto        localeCode = player.getLocaleCode();
            auto const& newName    = ev.getNewHome().name;

            if (!string_utils::isLengthValid(newName, getConfig().modules.home.nameLength)) {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Độ dài tên nhà không hợp lệ ({}/{})"_trl(
                        localeCode,
                        string_utils::length(newName),
                        getConfig().modules.home.nameLength
                    )
                );
                ev.cancel();
            }
        },
        ll::event::EventPriority::High
    ));

    mListeners.emplace_back(bus.emplaceListener<AdminRequestRemovePlayerHomeEvent>(
        [this](AdminRequestRemovePlayerHomeEvent& ev) {
            auto& bus    = ll::event::EventBus::getInstance();
            auto& player = ev.getAdmin();
            auto& target = ev.getTarget();
            auto& home   = ev.getHome();

            auto ing = AdminRemovingPlayerHomeEvent{player, target, home};
            bus.publish(ing);

            if (ing.isCancelled()) {
                return;
            }

            auto storage = this->getStorage();
            if (auto res = storage->removeHome(target, home.name)) {
                mc_utils::sendText(player, "Xóa nhà {} của người chơi {} thành công"_trl(player.getLocaleCode(), home.name, target));
            } else {
                mc_utils::sendText<mc_utils::Error>(
                    player,
                    "Xóa nhà {} của người chơi {} thất bại: {}"_trl(player.getLocaleCode(), home.name, target, res.error())
                );
                return;
            }

            auto rm = AdminRemovedPlayerHomeEvent{player, target, home};
            bus.publish(rm);
        },
        ll::event::EventPriority::High
    ));

    HomeCommand::setup();

    return true;
}

bool HomeModule::disable() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& ptr : mListeners) {
        bus.removeListener(ptr);
    }
    mListeners.clear();

    return true;
}

HomeStorage* HomeModule::getStorage() const { return getStorageManager().getStorage<HomeStorage>(); }

Cooldown& HomeModule::getCooldown() { return mCooldown; }

} // namespace ltps::home