#include "HomeOperatorGUI.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ltps/Global.h"
#include "ltps/TeleportSystem.h"
#include "ltps/common/BackSimpleForm.h"
#include "ltps/modules/home/HomeStorage.h"
#include "ltps/modules/home/event/HomeEvents.h"
#include "ltps/modules/home/gui/HomeOperatorGUI.h"
#include "ltps/utils/McUtils.h"
#include "mc/world/level/dimension/VanillaDimensions.h"
#include <utility>


namespace ltps::home {

using ll::form::CustomForm;
using ll::form::SimpleForm;

void HomeOperatorGUI::sendMainGUI(Player& player) {
    sendChoosePlayerGUI(player, [](Player& self, RealName targetPlayer) {
        sendChooseHomeGUI(self, std::move(targetPlayer), sendOperatorMenu);
    });
}

void HomeOperatorGUI::sendChoosePlayerGUI(Player& player, ChoosePlayerCallback callback) {
    auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<HomeStorage>();
    if (!storage) {
        return;
    }

    auto localeCode = player.getLocaleCode();

    SimpleForm fm{"Teleport System - Home Manager"_trl(localeCode)};
    fm.setContent("请选择一个玩家: "_trl(localeCode));

    auto const& map = storage->getAllHomes();
    for (auto const& pair : map) {
        fm.appendButton(pair.first, [callback, target = pair.first](Player& self) { callback(self, target); });
    }

    fm.sendTo(player);
}

void HomeOperatorGUI::sendChooseHomeGUI(Player& player, RealName targetPlayer, ChooseHomeCallback callback) {
    auto storage = TeleportSystem::getInstance().getStorageManager().getStorage<HomeStorage>();
    if (!storage) {
        return;
    }

    auto localeCode = player.getLocaleCode();

    auto& homes = storage->getHomes(targetPlayer);

    auto fm = BackSimpleForm::make<sendMainGUI>();
    fm.setTitle("Teleport System - Home Manager"_trl(localeCode));
    fm.setContent("{} 共有 {} 个传送点, 请选择一个: "_trl(localeCode, targetPlayer, homes.size()));

    fm.appendButton("创建"_trl(localeCode), "textures/ui/color_plus", "path", [targetPlayer](Player& self) {
        sendCreateOrEditHomeGUI(self, targetPlayer);
    });

    for (auto const& home : homes) {
        auto _home = home;
        fm.appendButton(home.name, [callback, target = targetPlayer, home = std::move(_home)](Player& self) {
            callback(self, target, home);
        });
    }

    fm.sendTo(player);
}

void HomeOperatorGUI::sendOperatorMenu(Player& player, RealName targetPlayer, HomeStorage::Home home) {
    auto localeCode = player.getLocaleCode();

    BackSimpleForm::make<sendChooseHomeGUI>(targetPlayer, sendOperatorMenu)
        .setTitle("Teleport System - Home Manager"_trl(localeCode))
        .setContent("所属玩家: {}\n家园名称: {}\n家园坐标: {}\n创建时间: {}\n修改时间: {}"_trl(
            localeCode,
            targetPlayer,
            home.name,
            home.toPosString(),
            home.createdTime,
            home.modifiedTime
        ))
        .appendButton(
            "前往"_trl(localeCode),
            "textures/ui/send_icon",
            "path",
            [targetPlayer, home](Player& self) {
                ll::event::EventBus::getInstance().publish(AdminRequestGoPlayerHomeEvent{self, targetPlayer, home});
            }
        )
        .appendButton(
            "编辑"_trl(localeCode),
            "textures/ui/book_edit_default",
            "path",
            [targetPlayer, home](Player& self) { sendCreateOrEditHomeGUI(self, targetPlayer, home); }
        )
        .appendButton(
            "删除"_trl(localeCode),
            "textures/ui/trash_default",
            "path",
            [targetPlayer, home](Player& self) {
                ll::event::EventBus::getInstance().publish(AdminRequestRemovePlayerHomeEvent{self, targetPlayer, home});
            }
        )
        .sendTo(player);
}

void HomeOperatorGUI::sendCreateOrEditHomeGUI(
    Player&                          player,
    RealName                         targetPlayer,
    std::optional<HomeStorage::Home> home
) {
    auto localeCode = player.getLocaleCode();

    CustomForm fm{"Home Manager - Create Home"_trl(localeCode)};
    fm.appendInput("name", "请输入家园名称: "_trl(localeCode), "string", home ? home->name : "");
    fm.appendInput(
        "pos",
        "请输入家园坐标: "_trl(localeCode),
        "string",
        (home ? "{},{},{}"_tr(home->x, home->y, home->z) : ""),
        "使用半角逗号分隔坐标, 例如: x,y,z"_tr(localeCode)
    );

    auto&                           dimMap = VanillaDimensions::DimensionMap();
    static std::vector<std::string> dimNames;
    if (dimNames.empty() || dimMap.mRight.size() != dimNames.size()) {
        dimNames.clear();
        dimNames.reserve(dimMap.mRight.size());
        for (auto const& pair : dimMap.mRight) {
            dimNames.push_back(pair.first);
        }
    }

    auto index = 0;
    if (home) {
        auto nameIter = dimMap.mLeft.find(home->dimid);
        if (nameIter != dimMap.mLeft.end()) {
            auto const& dimName = nameIter->second; // std::string
            auto        it      = std::find(dimNames.begin(), dimNames.end(), dimName);
            if (it != dimNames.end()) {
                index = static_cast<int>(std::distance(dimNames.begin(), it));
            }
        }
    }

    fm.appendDropdown("dimName", "请选择一个维度: "_trl(localeCode), dimNames, index);

    fm.sendTo(
        player,
        [targetPlayer = std::move(targetPlayer),
         localeCode   = std::move(localeCode),
         home         = std::move(home)](Player& self, ll::form::CustomFormResult const& result, auto) {
            if (!result) {
                return;
            }

            auto name = std::get<std::string>(result->at("name"));

            int dimid = -1;
            {
                auto  dimName = std::get<std::string>(result->at("dimName"));
                auto& dimMap  = VanillaDimensions::DimensionMap();
                auto  dimIter = dimMap.mRight.find(dimName);
                if (dimIter == dimMap.mRight.end()) {
                    mc_utils::sendText<mc_utils::Error>(self, "无效的维度名称"_trl(localeCode));
                    return;
                }
                dimid = dimIter->second;
            }

            Vec3 v3; // x, y, z
            {
                auto posStr = std::get<std::string>(result->at("pos"));

                std::vector<std::string> parts;
                std::istringstream       ss(posStr);
                std::string              part;
                while (std::getline(ss, part, ',')) {
                    parts.push_back(part);
                }
                if (parts.size() != 3) {
                    mc_utils::sendText<mc_utils::Error>(self, "坐标格式错误"_trl(localeCode));
                    return;
                } else {
                    try {
                        v3.x = std::stof(parts[0]);
                        v3.y = std::stof(parts[1]);
                        v3.z = std::stof(parts[2]);
                    } catch (...) {
                        mc_utils::sendText<mc_utils::Error>(self, "捕获到异常，请检查坐标"_trl(localeCode));
                        return;
                    }
                }
            }

            if (home) {
                auto newHome = HomeStorage::Home::make(v3, dimid, name);
                ll::event::EventBus::getInstance().publish(
                    AdminRequestEditPlayerHomeEvent{self, targetPlayer, home.value(), newHome}
                );
            } else {
                ll::event::EventBus::getInstance().publish(
                    AdminRequestCreateHomeForPlayerEvent{self, targetPlayer, name, dimid, v3}
                );
            }
        }
    );
}


} // namespace ltps::home