#include "SafeTeleport.h"
#include "ll/api/chrono/GameChrono.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "ltps/Global.h"
#include "ltps/LeviTPSystem.h"
#include "ltps/base/Config.h"
#include "ltps/utils/McUtils.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/world/actor/player/Player.h"
#include <cstdint>
#include <ll/api/coro/CoroTask.h>
#include <ll/api/thread/ThreadPoolExecutor.h>
#include <mc/deps/core/math/Vec3.h>
#include <mc/deps/game_refs/WeakRef.h>
#include <mc/world/level/BlockSource.h>
#include <mc/world/level/block/Block.h>
#include <mc/world/level/chunk/ChunkSource.h>
#include <mc/world/level/chunk/ChunkState.h>
#include <mc/world/level/chunk/LevelChunk.h>
#include <mc/world/level/dimension/Dimension.h>

namespace ltps::tpr {


bool SafeTeleport::Task::operator==(const Task& other) const { return mId == other.mId; }

inline SafeTeleport::TaskId NextTaskId = 0;
SafeTeleport::Task::Task(Player& player, DimensionPos targetPos)
: mId(NextTaskId++),
  mWeakPlayer(player.getWeakEntity()),
  mChunkSource(player.getDimensionBlockSource().getChunkSource()),
  mTargetChunkPos(ChunkPos(targetPos.first)),
  mCachedLocaleCode(player.getLocaleCode()),
  mSourcePos({player.getPosition(), player.getDimensionId()}),
  mTargetPos(targetPos) {}

bool SafeTeleport::Task::isPending() const { return mState == TaskState::Pending; }
bool SafeTeleport::Task::isWaitingChunkLoad() const { return mState == TaskState::WaitingChunkLoad; }
bool SafeTeleport::Task::isChunkLoadTimeout() const { return mState == TaskState::ChunkLoadTimeout; }
bool SafeTeleport::Task::isChunkLoaded() const { return mState == TaskState::ChunkLoaded; }
bool SafeTeleport::Task::isFindingSafePos() const { return mState == TaskState::FindingSafePos; }
bool SafeTeleport::Task::isFoundSafePos() const { return mState == TaskState::FoundSafePos; }
bool SafeTeleport::Task::isNoSafePos() const { return mState == TaskState::NoSafePos; }
bool SafeTeleport::Task::isTaskCompleted() const { return mState == TaskState::TaskCompleted; }
bool SafeTeleport::Task::isTaskFailed() const { return mState == TaskState::TaskFailed; }
bool SafeTeleport::Task::isAborted() const { return mAbortFlag.load(); }

SafeTeleport::TaskState SafeTeleport::Task::getState() const { return mState; }

Player* SafeTeleport::Task::getPlayer() const { return mWeakPlayer.tryUnwrap<Player>().as_ptr(); }

void SafeTeleport::Task::updateState(TaskState state) { mState = state; }

void SafeTeleport::Task::updateCounter() { mCounter++; }

void SafeTeleport::Task::sendWaitChunkLoadTip() {
    if (auto player = getPlayer()) {
        mTipPacket.mTitleText = "等待区块加载... ({}/{})"_trl(mCachedLocaleCode, mCounter, MaxCounter);
        mTipPacket.sendTo(*player);
    }
}

void SafeTeleport::Task::abort() {
    mAbortFlag.store(true);
    updateState(TaskState::TaskFailed);
}

void SafeTeleport::Task::rollback() const {
    if (auto player = getPlayer()) {
        player->teleport(mSourcePos.first, mSourcePos.second);
    }
}

void SafeTeleport::Task::commit() const {
    if (auto player = getPlayer()) {
        player->teleport(mTargetPos.first, mTargetPos.second);
    }
}

bool SafeTeleport::Task::isTargetChunkFullyLoaded() const {
    if (!mChunkSource.isWithinWorldLimit(mTargetChunkPos)) return true;
    auto chunk = mChunkSource.getOrLoadChunk(mTargetChunkPos, ::ChunkSource::LoadMode::None, true);
    return chunk && static_cast<int>(chunk->mLoadState->load()) >= static_cast<int>(ChunkState::Loaded)
        && !chunk->mIsEmptyClientChunk && chunk->mIsRedstoneLoaded;
}

void SafeTeleport::Task::checkChunkStatus() {
    if (isWaitingChunkLoad()) {
        if (isTargetChunkFullyLoaded()) {
            updateState(TaskState::ChunkLoaded);
        } else if (mCounter > MaxCounter) {
            updateState(TaskState::ChunkLoadTimeout);
        } else {
            updateCounter();
            sendWaitChunkLoadTip();
        }
    }
}
void SafeTeleport::Task::checkPlayerStatus() {
    if (!getPlayer()) {
        updateState(TaskState::TaskFailed);
    }
}


void SafeTeleport::Task::_findSafePos() {
    auto const& dangerousBlocks = getConfig().modules.tpr.dangerousBlocks;

    auto& targetPos   = mTargetPos.first;
    auto* player      = getPlayer();
    auto& blockSource = player->getDimensionBlockSource();

    auto&      heightRange = player->getDimension().mHeightRange;
    auto const start       = heightRange->mMax;
    auto const end         = heightRange->mMin;

    Block* headBlock = nullptr; // 头部方块
    Block* legBlock  = nullptr; // 腿部方块

    auto& y = targetPos.y;
    y       = start; // 从最高点开始寻找

    while (y > end && !mAbortFlag.load()) {
        auto block = &const_cast<Block&>(blockSource.getBlock(targetPos));

        if (!headBlock && !legBlock) { // 第一次循环, 初始化
            headBlock = block;
            legBlock  = block;
        }

        if (!block->isAir() &&                                 // 落脚点不是空气
            !dangerousBlocks.contains(block->getTypeName()) && // 落脚点不是危险方块
            headBlock->isAir() &&                              // 头部方块是空气
            legBlock->isAir()                                  // 腿部方块是空气
        ) {
            updateState(TaskState::FoundSafePos); // 找到安全位置
            return;
        }

        headBlock = legBlock;
        legBlock  = block;
        y--;
    }
    updateState(TaskState::NoSafePos); // 没有找到安全位置
}

void SafeTeleport::Task::launchFindPosTask() {
    ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
        co_await ll::chrono::ticks(1); // 等待 1_tick 再开始寻找安全位置
        _findSafePos();
        co_return;
    }).launch(ll::thread::ServerThreadExecutor::getDefault());
}


SafeTeleport::SafeTeleport() {
    ll::coro::keepThis([this]() -> ll::coro::CoroTask<> {
        while (!mPollingStopFlag.load()) {
            co_await ll::chrono::ticks(5); // 每 5_tick 检查一次任务状态
            try {
                polling();
            } catch (...) {
                LeviTPSystem::getInstance().getSelf().getLogger().error(
                    "An exception occurred while polling SafeTeleport tasks"
                );
            }
        }
        co_return;
    });
}

SafeTeleport::~SafeTeleport() {
    mPollingStopFlag.store(true);
    for (auto& [_, task] : mTasks) {
        task->abort();
    }
    mTasks.clear();
}

void SafeTeleport::launchTask(Player& player, DimensionPos targetPos) {
    auto task = std::make_shared<Task>(player, targetPos);
    mTasks.emplace(task->mId, task);
}

void SafeTeleport::polling() {
    auto iter = mTasks.begin();

    while (iter != mTasks.end()) {
        auto& task = iter->second;

        if (!task->isTaskFailed() && !task->isTaskCompleted()) {
            task->checkPlayerStatus(); // 检查玩家是否在线
        }

        switch (task->getState()) {
        case TaskState::Pending:
            handlePending(task);
            break;
        case TaskState::WaitingChunkLoad:
            handleWaitingChunkLoad(task);
            break;
        case TaskState::ChunkLoadTimeout:
            handleChunkLoadTimeout(task);
            break;
        case TaskState::ChunkLoaded:
            handleChunkLoaded(task);
            break;
        case TaskState::FindingSafePos:
            break;
        case TaskState::FoundSafePos:
            handleFoundSafePos(task);
            break;
        case TaskState::NoSafePos:
            handleNoSafePos(task);
            break;
        case TaskState::TaskCompleted:
        case TaskState::TaskFailed:
            iter = mTasks.erase(iter); // 任务完成或失败, 移除任务
            break;
        }
    }
}

void SafeTeleport::handlePending(SharedTask& task) {
    mc_utils::sendText(*task->getPlayer(), "[1/4] 任务已创建"_trl(task->mCachedLocaleCode));

    if (task->isTargetChunkFullyLoaded()) {
        task->updateState(TaskState::ChunkLoaded);
    } else {
        task->updateState(TaskState::WaitingChunkLoad);
        mc_utils::sendText(
            *task->getPlayer(),
            "[2/4] 目标区块未加载，等待目标区块加载..."_trl(task->mCachedLocaleCode)
        );
    }
}
void SafeTeleport::handleWaitingChunkLoad(SharedTask& task) { task->checkChunkStatus(); }
void SafeTeleport::handleChunkLoadTimeout(SharedTask& task) {
    mc_utils::sendText(*task->getPlayer(), "[2/4] 目标区块加载超时，正在返回原位置..."_trl(task->mCachedLocaleCode));
    task->rollback();
    task->updateState(TaskState::TaskFailed);
}
void SafeTeleport::handleChunkLoaded(SharedTask& task) {
    mc_utils::sendText(*task->getPlayer(), "[3/4] 区块已加载，正在寻找安全位置..."_trl(task->mCachedLocaleCode));
    task->launchFindPosTask();
    task->updateState(TaskState::FindingSafePos);
}

void SafeTeleport::handleFoundSafePos(SharedTask& task) {
    mc_utils::sendText(*task->getPlayer(), "[4/4] 安全位置已找到，正在传送..."_trl(task->mCachedLocaleCode));
    task->commit();
    task->updateState(TaskState::TaskCompleted);
}
void SafeTeleport::handleNoSafePos(SharedTask& task) {
    mc_utils::sendText(*task->getPlayer(), "[3/4] 未找到安全位置，正在返回原位置..."_trl(task->mCachedLocaleCode));
    task->rollback();
    task->updateState(TaskState::TaskFailed);
}


} // namespace ltps::tpr