
#include "include/Core/ReplayRecorder.hpp"

#include "System/Action_1.hpp"
#include "System/Threading/Tasks/Task.hpp"
#include "System/Threading/Tasks/Task_1.hpp"
#include "System/Collections/Generic/HashSet_1.hpp"

#include "include/Models/Replay.hpp"

#include "include/Enhancers/MapEnhancer.hpp"
#include "include/Enhancers/UserEnhancer.hpp"

#include "include/Utils/ReplayManager.hpp"
#include "include/Utils/RecorderUtils.hpp"
#include "include/Utils/ModConfig.hpp"
#include "include/API/PlayerController.hpp"

#include "beatsaber-hook/shared/utils/hooking.hpp"

#include "config-utils/shared/config-utils.hpp"
#include "custom-types/shared/register.hpp"

#include "UnityEngine/Application.hpp"
#include "UnityEngine/Resources.hpp"
#include "UnityEngine/Time.hpp"

#include "GlobalNamespace/NoteController.hpp"
#include "GlobalNamespace/SoloFreePlayFlowCoordinator.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/BeatmapData.hpp"
#include "GlobalNamespace/GameplayModifiers.hpp"
#include "GlobalNamespace/LevelCompletionResults.hpp"
#include "GlobalNamespace/StandardLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/BeatmapEnvironmentHelper.hpp"
#include "GlobalNamespace/OverrideEnvironmentSettings.hpp"
#include "GlobalNamespace/GameplayCoreInstaller.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"
#include "GlobalNamespace/BeatmapObjectSpawnMovementData.hpp"
#include "GlobalNamespace/BeatmapObjectData.hpp"
#include "GlobalNamespace/NoteData.hpp"
#include "GlobalNamespace/CutScoreBuffer.hpp"
#include "GlobalNamespace/BeatmapObjectSpawnMovementData_NoteSpawnData.hpp"
#include "GlobalNamespace/ObstacleData.hpp"
#include "GlobalNamespace/BeatmapObjectSpawnMovementData_ObstacleSpawnData.hpp"
#include "GlobalNamespace/NoteController.hpp"
#include "GlobalNamespace/ObstacleController.hpp"
#include "GlobalNamespace/BeatmapObjectManager.hpp"
#include "GlobalNamespace/ScoreController.hpp"
#include "GlobalNamespace/NoteCutInfo.hpp"
#include "GlobalNamespace/BeatmapObjectSpawnController.hpp"
#include "GlobalNamespace/ISaberSwingRatingCounterDidChangeReceiver.hpp"
#include "GlobalNamespace/ISaberSwingRatingCounterDidFinishReceiver.hpp"
#include "GlobalNamespace/ISaberSwingRatingCounter.hpp"
#include "GlobalNamespace/PlayerHeadAndObstacleInteraction.hpp"
#include "GlobalNamespace/PlayerTransforms.hpp"
#include "GlobalNamespace/PauseMenuManager.hpp"
#include "GlobalNamespace/SaberSwingRatingCounter.hpp"
#include "GlobalNamespace/PlayerHeightDetector.hpp"
#include "GlobalNamespace/BeatmapDifficulty.hpp"
#include "GlobalNamespace/IBeatmapLevel.hpp"
#include "GlobalNamespace/IBeatmapLevelData.hpp"
#include "GlobalNamespace/IDifficultyBeatmapSet.hpp"
#include "GlobalNamespace/IReadonlyBeatmapData.hpp"
#include "GlobalNamespace/BeatmapCharacteristicSO.hpp"
#include "GlobalNamespace/IPreviewBeatmapLevel.hpp"
#include "GlobalNamespace/SaberMovementData.hpp"
#include "GlobalNamespace/SaberSwingRating.hpp"
#include "GlobalNamespace/GameplayCoreSceneSetupData.hpp"

#include "GlobalNamespace/ScoringElement.hpp"
#include "GlobalNamespace/BadCutScoringElement.hpp"
#include "GlobalNamespace/GoodCutScoringElement.hpp"
#include "GlobalNamespace/MissScoringElement.hpp"
#include "GlobalNamespace/MultiplayerController.hpp"
#include "GlobalNamespace/MultiplayerLevelScenesTransitionSetupDataSO.hpp"
#include "GlobalNamespace/MultiplayerResultsData.hpp"
#include "GlobalNamespace/MultiplayerPlayerResultsData.hpp"
#include "GlobalNamespace/MultiplayerLevelCompletionResults.hpp"
#include "GlobalNamespace/PlayersSpecificSettingsAtGameStartModel.hpp"
#include "GlobalNamespace/PlayerSpecificSettingsNetSerializable.hpp"

#include "main.hpp"

#include <map>
#include <chrono>
#include <iostream>
#include <sstream>
#include <regex>

using namespace GlobalNamespace;
using UnityEngine::Resources;

namespace ReplayRecorder {

    optional<Replay> replay;
    std::function<void(Replay const&, MapStatus, bool)> replayCallback;

    MapEnhancer mapEnhancer;
    UserEnhancer userEnhancer;

    AudioTimeSyncController* audioTimeSyncController;
    bool automaticPlayerHeight = false;
    PlayerHeadAndObstacleInteraction* phoi;

    map<int, NoteEvent> _noteEventCache;
    map<SaberMovementData *, float> _preSwingContainer;
    map<SaberSwingRatingCounter *, float> _postSwingContainer;

    static map<NoteData *, int> _noteIdCache;
    static map<NoteData *, int> _replayNoteIdCache;
    map<ISaberSwingRatingCounter*, int> _swingIdCache;
    int _noteId;

    map<ObstacleController *, int> _wallCache;
    map<int, WallEvent> _wallEventCache;
    int _wallId;

    optional<Pause> _currentPause;
    optional<WallEvent> _currentWallEvent;
    chrono::steady_clock::time_point _pauseStartTime;
    System::Action_1<float>* _heightEvent;
    System::Action_1<ScoringElement*>* _scoreEvent;

    bool isOst = false;
    void collectMapData(StandardLevelScenesTransitionSetupDataSO* self) {
        isOst = !self->gameplayCoreSceneSetupData->previewBeatmapLevel->get_levelID().starts_with("custom_level");

        mapEnhancer.difficultyBeatmap = self->difficultyBeatmap;
        mapEnhancer.previewBeatmapLevel = self->gameplayCoreSceneSetupData->previewBeatmapLevel;
        mapEnhancer.gameplayModifiers = self->gameplayModifiers;
        mapEnhancer.playerSpecificSettings = self->gameplayCoreSceneSetupData->playerSpecificSettings;
        mapEnhancer.practiceSettings = self->practiceSettings;
        mapEnhancer.environmentInfo = self->environmentInfo;
        mapEnhancer.colorScheme = self->colorScheme;
    }

    void collectMultiplayerMapData(MultiplayerLevelScenesTransitionSetupDataSO* self) {
        GameplayCoreSceneSetupData* gameplayCoreSceneSetupData = reinterpret_cast<GameplayCoreSceneSetupData*>(self->sceneSetupDataArray->get(2));

        isOst = !to_utf8(csstrtostr(gameplayCoreSceneSetupData->previewBeatmapLevel->get_levelID())).starts_with("custom_level");

        mapEnhancer.difficultyBeatmap = self->difficultyBeatmap;
        mapEnhancer.previewBeatmapLevel = gameplayCoreSceneSetupData->previewBeatmapLevel;
        mapEnhancer.gameplayModifiers = gameplayCoreSceneSetupData->gameplayModifiers;
        mapEnhancer.playerSpecificSettings = gameplayCoreSceneSetupData->playerSpecificSettings;
        mapEnhancer.practiceSettings = NULL;
        mapEnhancer.environmentInfo = self->multiplayerEnvironmentInfo;
        mapEnhancer.colorScheme = self->colorScheme;
    }

    void OnPlayerHeightChange(float height)
    {
        if (audioTimeSyncController && automaticPlayerHeight && replay != nullopt) {
            replay->heights.emplace_back(height, audioTimeSyncController->songTime);
        }
    }

    void startReplay() {
        std::string timeStamp(std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));

        recording = true;
        _currentPause = nullopt;
        replay.emplace(ReplayInfo(modInfo.version, UnityEngine::Application::get_version(), timeStamp));

        userEnhancer.Enhance(replay.value());
       
    }

    MAKE_HOOK_MATCH(MultiplayerLevelPlay, &MultiplayerController::StartGameplay, void, MultiplayerController* self, float syncTime) {
        MultiplayerLevelPlay(self, syncTime);

        if (PlayerController::currentPlayer == std::nullopt || !UploadEnabled()) {
            replay = nullopt;
            return;
        }

        startReplay();
        
        automaticPlayerHeight = self->playersSpecificSettingsAtGameStartModel->localPlayerSpecificSettings->automaticPlayerHeight;;
    }

    MAKE_HOOK_MATCH(LevelPlay, &SinglePlayerLevelSelectionFlowCoordinator::StartLevel, void, SinglePlayerLevelSelectionFlowCoordinator* self, System::Action* beforeSceneSwitchCallback, bool practice) {
        LevelPlay(self, beforeSceneSwitchCallback, practice);

        if (PlayerController::currentPlayer == std::nullopt || !UploadEnabled()) {
            replay = nullopt;
            return;
        }

        startReplay();
        automaticPlayerHeight = self->get_playerSettings()->automaticPlayerHeight;
    }

    MAKE_HOOK_MATCH(PlayerHeightDetectorStart, &PlayerHeightDetector::Start, void, PlayerHeightDetector* self) {
        PlayerHeightDetectorStart(self);
        if (replay == nullopt) return;

        _heightEvent = il2cpp_utils::MakeDelegate<System::Action_1<float> *>(
                        classof(System::Action_1<float>*),
                        static_cast<Il2CppObject *>(nullptr), OnPlayerHeightChange);
        self->add_playerHeightDidChangeEvent(_heightEvent);
    }

    void processResults(LevelCompletionResults* levelCompletionResults) {
        replay->info.score = levelCompletionResults->multipliedScore;

        mapEnhancer.energy = levelCompletionResults->energy;
        mapEnhancer.Enhance(replay.value());
        
        switch (levelCompletionResults->levelEndStateType)
        {
            case LevelCompletionResults::LevelEndStateType::Cleared:
                replayCallback(*replay, MapStatus::cleared, isOst);
                break;
            case LevelCompletionResults::LevelEndStateType::Failed:
                if (levelCompletionResults->levelEndAction != LevelCompletionResults::LevelEndAction::Restart)
                {
                    replay->info.failTime = audioTimeSyncController->songTime;
                    replayCallback(*replay, MapStatus::failed, isOst);
                }
                break;
        }
    }

    MAKE_HOOK_MATCH(ProcessResultsSolo, &StandardLevelScenesTransitionSetupDataSO::Finish, void, StandardLevelScenesTransitionSetupDataSO* self, LevelCompletionResults* levelCompletionResults) {
        ProcessResultsSolo(self, levelCompletionResults);
        recording = false;
        if (self->gameMode != "Party" && replay != nullopt) {
            collectMapData(self);
            processResults(levelCompletionResults);
        }
    }

    void processMultiplayerResults(MultiplayerResultsData* levelCompletionResults) {
        auto results = levelCompletionResults->localPlayerResultData->multiplayerLevelCompletionResults;

        if (results->get_hasAnyResults()) {
            switch (results->playerLevelEndReason)
            {
                case MultiplayerLevelCompletionResults::MultiplayerPlayerLevelEndReason::Cleared:
                    auto results = levelCompletionResults->localPlayerResultData->multiplayerLevelCompletionResults->levelCompletionResults; 

                    replay->info.score = results->multipliedScore;

                    mapEnhancer.energy = results->energy;
                    mapEnhancer.Enhance(replay.value());
                    replayCallback(*replay, MapStatus::cleared, isOst);
                    break;
            }
        }
    }

    MAKE_HOOK_MATCH(ProcessResultsMultiplayer, &MultiplayerLevelScenesTransitionSetupDataSO::Finish, void, MultiplayerLevelScenesTransitionSetupDataSO* self, MultiplayerResultsData* levelCompletionResults) {
        ProcessResultsMultiplayer(self, levelCompletionResults);
        recording = false;
        if (replay != nullopt && levelCompletionResults != NULL) {
            collectMultiplayerMapData(self);
            processMultiplayerResults(levelCompletionResults);
        }
    }

    void NoteSpawned(NoteController* noteController, NoteData* noteData) {
        if (replay == nullopt) return;
        _noteId++;
        _noteIdCache[noteData] = _noteId;

        int colorType = (int)noteData->colorType;
        if (colorType < 0) {
            colorType = 3;
        }
        auto noteID = ((int)noteData->scoringType + 2) * 10000 + noteData->lineIndex * 1000 + (int)noteData->noteLineLayer * 100 + colorType * 10 + (int)noteData->cutDirection;
        auto spawnTime = noteData->time;
        _noteEventCache.emplace(_noteId, NoteEvent(noteID, spawnTime));
    }

    MAKE_HOOK_MATCH(SpawnNote, &BeatmapObjectManager::AddSpawnedNoteController, void, BeatmapObjectManager* self, NoteController* noteController, BeatmapObjectSpawnMovementData::NoteSpawnData noteSpawnData, float rotation) {
        SpawnNote(self, noteController, noteSpawnData, rotation);
        NoteSpawned(noteController, noteController->noteData);
    }

    MAKE_HOOK_MATCH(SpawnObstacle, &BeatmapObjectManager::AddSpawnedObstacleController, void, BeatmapObjectManager* self, ObstacleController* obstacleController, BeatmapObjectSpawnMovementData::ObstacleSpawnData obstacleSpawnData, float rotation) {
        SpawnObstacle(self, obstacleController, obstacleSpawnData, rotation);

        if (replay != nullopt && audioTimeSyncController != NULL && obstacleController->obstacleData != NULL) {
            int wallId = _wallId++;
            _wallCache[obstacleController] = wallId;

            auto wallID = obstacleController->obstacleData->lineIndex * 100 + (int)obstacleController->obstacleData->type * 10 + obstacleController->obstacleData->width;
            auto spawnTime = audioTimeSyncController->songTime;

            _wallEventCache.emplace(wallId, WallEvent(wallID, spawnTime));
        }
    }

    void PopulateNoteCutInfo(ReplayNoteCutInfo& noteCutInfo, NoteCutInfo const& cutInfo) {
        noteCutInfo.speedOK = cutInfo.speedOK;
        noteCutInfo.directionOK = cutInfo.directionOK;
        noteCutInfo.saberTypeOK = cutInfo.saberTypeOK;
        noteCutInfo.wasCutTooSoon = cutInfo.wasCutTooSoon;
        noteCutInfo.saberSpeed = cutInfo.saberSpeed;
        noteCutInfo.saberDir = cutInfo.saberDir;
        noteCutInfo.saberType = (int)cutInfo.saberType;
        noteCutInfo.timeDeviation = cutInfo.timeDeviation;
        noteCutInfo.cutDirDeviation = cutInfo.cutDirDeviation;
        noteCutInfo.cutPoint = cutInfo.cutPoint;
        noteCutInfo.cutNormal = cutInfo.cutNormal;
        noteCutInfo.cutDistanceToCenter = cutInfo.cutDistanceToCenter;
        noteCutInfo.cutAngle = cutInfo.cutAngle;
    }

    static float ChooseSwingRating(float real, float unclamped) {
        return real < 1 ? real : max(real, unclamped);
    }

    void scoringElementFinished(ScoringElement* scoringElement) {
        if (replay == nullopt) return;

        auto noteData = scoringElement->noteData;

        if (!_replayNoteIdCache.count(noteData)) return;
        int noteId = _replayNoteIdCache[noteData];
        if (replay->notes.size() <= noteId) return;
        NoteEvent& noteEvent = replay->notes.at(noteId);
        bool isBomb = noteData->colorType == ColorType::None;
        
        if (il2cpp_utils::try_cast<MissScoringElement>(scoringElement) != nullopt) {
            if (isBomb) return;

            noteEvent.eventType = NoteEventType::MISS;
        } else if (il2cpp_utils::try_cast<BadCutScoringElement>(scoringElement) != nullopt) {
            noteEvent.eventType = isBomb ? NoteEventType::BOMB : NoteEventType::BAD;
        } else if (il2cpp_utils::try_cast<GoodCutScoringElement>(scoringElement) != nullopt) {
            GoodCutScoringElement* goodCut = il2cpp_utils::try_cast<GoodCutScoringElement>(scoringElement).value();
            CutScoreBuffer* cutScoreBuffer = goodCut->cutScoreBuffer;
            SaberSwingRatingCounter* saberSwingRatingCounter = cutScoreBuffer->saberSwingRatingCounter;

            ReplayNoteCutInfo& noteCutInfo = noteEvent.noteCutInfo;
            PopulateNoteCutInfo(noteCutInfo, cutScoreBuffer->noteCutInfo);
            
            noteCutInfo.beforeCutRating = ChooseSwingRating(saberSwingRatingCounter->beforeCutRating, _preSwingContainer[(SaberMovementData *)saberSwingRatingCounter->saberMovementData]);
            noteCutInfo.afterCutRating = ChooseSwingRating(saberSwingRatingCounter->afterCutRating, _postSwingContainer[saberSwingRatingCounter]);

            _preSwingContainer[(SaberMovementData *)saberSwingRatingCounter->saberMovementData] = 0;
            _postSwingContainer[saberSwingRatingCounter] = 0;
        }
    }

    MAKE_HOOK_MATCH(ScoreControllerLateUpdate, &ScoreController::LateUpdate, void, ScoreController* self) {
        auto sortedScoringElementsWithoutMultiplier = self->sortedScoringElementsWithoutMultiplier;
        auto sortedNoteTimesWithoutScoringElements = self->sortedNoteTimesWithoutScoringElements;

        if (replay != nullopt
            && sortedScoringElementsWithoutMultiplier != NULL 
            && sortedNoteTimesWithoutScoringElements != NULL
            && self->audioTimeSyncController != NULL) {
            auto songTime = self->audioTimeSyncController->songTime;

            float nearestNotCutNoteTime = sortedNoteTimesWithoutScoringElements->get_Count() > 0 ? sortedNoteTimesWithoutScoringElements->get_Item(0) : 10000000;
            float skipAfter = songTime + 0.15f;

            for (int i = 0; i < sortedScoringElementsWithoutMultiplier->get_Count(); i++) {
                auto scoringElement = sortedScoringElementsWithoutMultiplier->get_Item(i);
                if (scoringElement->get_time() >= skipAfter && scoringElement->get_time() <= nearestNotCutNoteTime) break;
                
                auto noteData = scoringElement->noteData;
                if (il2cpp_utils::try_cast<MissScoringElement>(scoringElement) != nullopt && (noteData->colorType == ColorType::None || noteData->scoringType == NoteData::ScoringType::NoScore)) continue;

                int noteId = _noteIdCache[noteData];
                NoteEvent& noteEvent = _noteEventCache.at(noteId);
                noteEvent.eventTime = songTime;

                if (replay != nullopt) {
                    replay->notes.push_back(noteEvent);
                    _replayNoteIdCache[noteData] = replay->notes.size() - 1;
                }
            }
        }

        ScoreControllerLateUpdate(self);
    }

    MAKE_HOOK_MATCH(ScoreControllerStart, &ScoreController::Start, void, ScoreController* self) {
        ScoreControllerStart(self);
        if (replay == nullopt) return;

        _scoreEvent = il2cpp_utils::MakeDelegate<System::Action_1<ScoringElement*> *>(
                        classof(System::Action_1<ScoringElement*>*),
                        static_cast<Il2CppObject *>(nullptr), scoringElementFinished);
        self->add_scoringForNoteFinishedEvent(_scoreEvent);

        audioTimeSyncController = self->audioTimeSyncController;
    }

    MAKE_HOOK_MATCH(ComputeSwingRating, static_cast<float (SaberMovementData::*)(bool, float)>(&SaberMovementData::ComputeSwingRating), float, SaberMovementData* self, bool overrideSegmenAngle, float overrideValue) {
        float result = ComputeSwingRating(self, overrideSegmenAngle, overrideValue);
        if (replay == nullopt) return result;
        auto _data = self->data;
        int _nextAddIndex = self->nextAddIndex;
        int _validCount = self->validCount;

        int length = _data.Length();

        int index = _nextAddIndex - 1;
        if (index < 0) index += length;

        float startTime = _data[index].time;
        float time = startTime;

        UnityEngine::Vector3 segmentNormal1 = _data[index].segmentNormal;
        float angleDiff = overrideSegmenAngle ? overrideValue : _data[index].segmentAngle;
        float swingRating = SaberSwingRating::BeforeCutStepRating(angleDiff, 0.0f);
        for (int i = 2; (double)startTime - (double)time < 0.4 && i < _validCount; ++i)
        {
            --index;
            if (index < 0) index += length;

            UnityEngine::Vector3 segmentNormal2 = _data[index].segmentNormal;
            float segmentAngle = _data[index].segmentAngle;

            float normalDiff = UnityEngine::Vector3::Angle(segmentNormal2, segmentNormal1);
            if ((double)normalDiff <= 90.0)
            {
                swingRating += SaberSwingRating::BeforeCutStepRating(segmentAngle, normalDiff);
                time = _data[index].time;
            }
            else {
                break;
            }
        }

        _preSwingContainer[self] = swingRating;
        return result;
    }

    MAKE_HOOK_MATCH(ProcessNewSwingData, &SaberSwingRatingCounter::ProcessNewData, void, SaberSwingRatingCounter* self, BladeMovementDataElement newData, BladeMovementDataElement prevData, bool prevDataAreValid) {
        bool alreadyCut = self->notePlaneWasCut;
        ProcessNewSwingData(self, newData, prevData, prevDataAreValid);
        if (replay == nullopt) return;

        float postSwing = _postSwingContainer[self];
        if (!alreadyCut && !self->notePlane.SameSide(newData.topPos, prevData.topPos))
        {
            float angleDiff = UnityEngine::Vector3::Angle(self->cutTopPos - self->cutBottomPos, self->afterCutTopPos - self->afterCutBottomPos);

            if (self->rateAfterCut)
            {
                postSwing = SaberSwingRating::AfterCutStepRating(angleDiff, 0.0f);
            }
        }
        else
        {
            float normalDiff = UnityEngine::Vector3::Angle(newData.segmentNormal, self->cutPlaneNormal);
            if (self->rateAfterCut)
            {
                postSwing += SaberSwingRating::AfterCutStepRating(newData.segmentAngle, normalDiff);
            }
        }

        _postSwingContainer[self] = postSwing;
    }

    MAKE_HOOK_MATCH(ComboMultiplierChanged, &ScoreController::HandlePlayerHeadDidEnterObstacles, void,  ScoreController* self) {
        ComboMultiplierChanged(self);
        if (self->scoreMultiplierCounter->ProcessMultiplierEvent(ScoreMultiplierCounter::MultiplierEventType::Negative) && self->playerHeadAndObstacleInteraction->intersectingObstacles->get_Count() > 0 && replay != nullopt) {
            auto obstacleEnumerator = self->playerHeadAndObstacleInteraction->intersectingObstacles->GetEnumerator();
            if(obstacleEnumerator.MoveNext()) {
                WallEvent& wallEvent = _wallEventCache.at(_wallCache[reinterpret_cast<ObstacleController*>(obstacleEnumerator.current)]);
                wallEvent.time = audioTimeSyncController->songTime;
                replay->walls.emplace_back(wallEvent);
                _currentWallEvent = wallEvent;
                phoi = self->playerHeadAndObstacleInteraction;
            }
        }
    }

    MAKE_HOOK_MATCH(BeatMapStart, &BeatmapObjectSpawnController::Start, void, BeatmapObjectSpawnController* self) {
        BeatMapStart(self);

        if(replay != nullopt) {
            replay->info.jumpDistance = self->get_jumpDistance();
            _currentPause = nullopt;
            _currentWallEvent = nullopt;
        }
    }

    MAKE_HOOK_MATCH(LevelPause, &PauseMenuManager::ShowMenu, void, PauseMenuManager* self) {
        LevelPause(self);
        if (replay == nullopt) return;

        _currentPause = Pause();
        _currentPause->time = audioTimeSyncController->songTime;
        _pauseStartTime = chrono::steady_clock::now();
    }

    MAKE_HOOK_MATCH(LevelUnpause, &PauseMenuManager::HandleResumeFromPauseAnimationDidFinish, void, PauseMenuManager* self) {
        LevelUnpause(self);

        _currentPause->duration = (long)chrono::duration_cast<std::chrono::seconds>(chrono::steady_clock::now() - _pauseStartTime).count();

        if (replay != nullopt) {
            replay->pauses.emplace_back(_currentPause.value());
            _currentPause = nullopt;
        }
    }

    MAKE_HOOK_MATCH(Tick, &PlayerTransforms::Update, void, PlayerTransforms* trans) {
        Tick(trans);
        if (replay == nullopt) return;

        if (audioTimeSyncController != nullptr && _currentPause == nullopt) {
            
            auto time = audioTimeSyncController->songTime;
            auto fps = 1.0f / UnityEngine::Time::get_deltaTime();
            
            auto head = Transform(trans->get_headPseudoLocalPos(), trans->get_headPseudoLocalRot());
            auto leftHand = Transform(trans->get_leftHandPseudoLocalPos(), trans->get_leftHandPseudoLocalRot());
            auto rightHand = Transform(trans->get_rightHandPseudoLocalPos(), trans->get_rightHandPseudoLocalRot());
            
            replay->frames.emplace_back(time, fps, head, leftHand, rightHand);
        }

        if (_currentWallEvent != nullopt) {
            if (phoi->intersectingObstacles->get_Count() == 0)
            {
                _currentWallEvent->energy = audioTimeSyncController->songTime;
                _currentWallEvent = nullopt;
            }
        }
    }

    void StartRecording(function<void(Replay const &, MapStatus, bool)> const &callback) {
        LoggerContextObject logger = getLogger().WithContext("load");

        getLogger().info("Installing ReplayRecorder hooks...");

        INSTALL_HOOK(logger, ProcessResultsSolo);
        INSTALL_HOOK(logger, LevelPlay);
        INSTALL_HOOK(logger, SpawnNote);
        INSTALL_HOOK(logger, SpawnObstacle);
        INSTALL_HOOK(logger, ComboMultiplierChanged);
        INSTALL_HOOK(logger, BeatMapStart);
        INSTALL_HOOK(logger, LevelPause);
        INSTALL_HOOK(logger, LevelUnpause);
        INSTALL_HOOK(logger, Tick);
        INSTALL_HOOK(logger, ComputeSwingRating);
        INSTALL_HOOK(logger, ProcessNewSwingData);
        INSTALL_HOOK(logger, PlayerHeightDetectorStart);
        INSTALL_HOOK(logger, ScoreControllerStart);
        INSTALL_HOOK(logger, ScoreControllerLateUpdate);
        INSTALL_HOOK(logger, MultiplayerLevelPlay);
        INSTALL_HOOK(logger, ProcessResultsMultiplayer);

        getLogger().info("Installed all ReplayRecorder hooks!");

        replayCallback = callback;
    }
}