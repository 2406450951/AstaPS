# 7.2 protocol gaps

Only messages the server code uses are listed.

## Opcodes with no 7.2 CmdId (92)

These are 0 in `PacketOpcodes`, so their handlers are not registered and packets built with them are not sent.

`AvatarChangeElementTypeRsp`, `AvatarDelNotify`, `AvatarPromoteReq`, `BuyBattlePassLevelRsp`, `CancelCoopTaskReq`, `CheckUgcStateReq`, `CheckUgcStateRsp`, `DelBackupAvatarTeamReq`, `DelMailReq`, `DelTeamEntityNotify`, `DeleteFriendReq`, `DungeonPlayerDieRsp`, `EndCameraSceneLookNotify`, `EnterTrialAvatarActivityDungeonRsp`, `EntityAiKillSelfNotify`, `ExecuteGadgetLuaRsp`, `ExecuteGroupTriggerRsp`, `GetFriendShowAvatarInfoReq`, `GetFriendShowNameCardInfoReq`, `GetHomeLevelUpRewardReq`, `GetHomeLevelUpRewardRsp`, `GetMailItemReq`, `GetPushTipsRewardRsp`, `GetWorldMpInfoRsp`, `HomeAvatarSummonEventRsp`, `HomeAvatarSummonFinishRsp`, `HomeChangeBgmReq`, `HomeChangeBgmRsp`, `HomeChangeEditModeReq`, `HomeChangeModuleReq`, `HomeResourceTakeFetterExpRsp`, `HomeResourceTakeHomeCoinRsp`, `HomeSceneInitFinishReq`, `HomeSceneJumpRsp`, `IceCreatureDataNotify`, `IceCreatureGadgetDisappearNotify`, `IceCreatureLikeReq`, `IceCreatureSetTypeGadgetReq`, `MarkNewNotify`, `MusicGameSettleReq`, `MusicGameSettleRsp`, `MusicGameStartRsp`, `NALBKOPFIFD`, `PlatformChangeRouteNotify`, `PlatformStartRouteNotify`, `PlatformStopRouteNotify`, `PlayerApplyEnterHomeNotify`, `PlayerApplyEnterHomeResultReq`, `PlayerApplyEnterHomeResultRsp`, `PlayerApplyEnterMpReq`, `PlayerApplyEnterMpResultReq`, `PlayerApplyEnterMpResultRsp`, `PlayerChatRsp`, `QuestDelNotify`, `ReadMailNotify`, `ReadPrivateChatRsp`, `ReceivedTrialAvatarActivityRewardReq`, `ReceivedTrialAvatarActivityRewardRsp`, `ReformFireworksRsp`, `SceneEntityDrownReq`, `SceneEntityDrownRsp`, `SceneEntityUpdateNotify`, `SceneKickPlayerReq`, `SetBattlePassViewedRsp`, `SetNameCardReq`, `SetPlayerHeadImageRsp`, `SetPlayerPropRsp`, `SetPlayerSignatureReq`, `ShowClientGuideNotify`, `SkipPlayerGameTimeReq`, `SkipPlayerGameTimeRsp`, `SnezhnayaTrainCallNotify`, `SnezhnayaTrainCallReq`, `SnezhnayaTrainSetRouteReq`, `SnezhnayaTrainStateNotify`, `TakeAchievementGoalRewardReq`, `TakeAchievementRewardReq`, `TowerBuffSelectReq`, `TowerBuffSelectRsp`, `TowerSurrenderRsp`, `TowerTeamSelectRsp`, `TpsAmmunitionChangeNotify`, `TpsEquipChangeNotify`, `UnfreezeGroupLimitNotify`, `UnlockNameCardNotify`, `UnlockPersonalLineReq`, `WatcherChangeNotify`, `WearTpsEquipReq`, `WearTpsEquipRsp`, `WidgetDoBagRsp`, `WorldPlayerReviveRsp`, `_TakeAllForgeOutputReq`

## Messages with no 7.2 counterpart (105)

Their field numbers are still the 7.1 ones.

`AABLEDJBAKK`, `AbilityMetaSpecialEnergy`, `AvatarChangeElementTypeRsp`, `AvatarDelNotify`, `AvatarPromoteReq`, `AvatarTraceEffectChangeNotify`, `BMJNJLKCODG`, `BPAEPJHBOFM`, `BuyBattlePassLevelRsp`, `CancelCoopTaskReq`, `CheckUgcStateReq`, `CheckUgcStateRsp`, `CheckUgcUpdateReq`, `DelBackupAvatarTeamReq`, `DelMailReq`, `DelTeamEntityNotify`, `DeleteFriendReq`, `DungeonPlayerDieRsp`, `EndCameraSceneLookNotify`, `EnterTrialAvatarActivityDungeonRsp`, `EntityAiKillSelfNotify`, `ExecuteGadgetLuaRsp`, `ExecuteGroupTriggerRsp`, `GetFriendShowAvatarInfoReq`, `GetFriendShowNameCardInfoReq`, `GetHomeLevelUpRewardReq`, `GetHomeLevelUpRewardRsp`, `GetMailItemReq`, `GetPushTipsRewardRsp`, `GetShopReqNew`, `GetShopRspNew`, `GetUgcBriefInfoReq`, `GetWorldMpInfoRsp`, `HomeAvatarSummonEventRsp`, `HomeAvatarSummonFinishRsp`, `HomeChangeBgmReq`, `HomeChangeBgmRsp`, `HomeChangeEditModeReq`, `HomeChangeModuleReq`, `HomeChooseModuleReq`, `HomeModuleUnlockNotify`, `HomeResourceTakeFetterExpRsp`, `HomeResourceTakeHomeCoinRsp`, `HomeSceneInitFinishReq`, `HomeSceneJumpRsp`, `IceCreatureDataNotify`, `IceCreatureGadgetDisappearNotify`, `IceCreatureGadgetInfo`, `IceCreatureLikeReq`, `IceCreatureSetTypeGadgetReq`, `MarkNewNotify`, `MusicGameSettleReq`, `NALBKOPFIFD`, `PlatformChangeRouteNotify`, `PlatformStartRouteNotify`, `PlatformStopRouteNotify`, `PlayerApplyEnterHomeNotify`, `PlayerApplyEnterHomeResultReq`, `PlayerApplyEnterHomeResultRsp`, `PlayerApplyEnterMpReq`, `PlayerApplyEnterMpResultReq`, `PlayerApplyEnterMpResultRsp`, `PlayerChatRsp`, `PropChangeDetailInfo`, `QuestDelNotify`, `ReadMailNotify`, `ReadPrivateChatRsp`, `ReceivedTrialAvatarActivityRewardReq`, `ReceivedTrialAvatarActivityRewardRsp`, `ReformFireworksRsp`, `SceneEntityDrownReq`, `SceneEntityDrownRsp`, `SceneEntityUpdateNotify`, `SceneKickPlayerReq`, `SetBattlePassViewedRsp`, `SetNameCardReq`, `SetPlayerHeadImageRsp`, `SetPlayerPropRsp`, `SetPlayerSignatureReq`, `ShowClientGuideNotify`, `SkipPlayerGameTimeReq`, `SkipPlayerGameTimeRsp`, `SnezhnayaTrainCallNotify`, `SnezhnayaTrainCallReq`, `SnezhnayaTrainSetRouteReq`, `SnezhnayaTrainStateNotify`, `TakeAchievementGoalRewardReq`, `TakeAchievementRewardReq`, `TowerBuffSelectReq`, `TowerBuffSelectRsp`, `TowerMiddleLevelChangeTeamNotify`, `TowerSurrenderRsp`, `TowerTeamSelectRsp`, `TpsAmmunitionChangeNotify`, `TpsEquipChangeNotify`, `TpsWeaponAmmunitionInfo`, `UnfreezeGroupLimitNotify`, `UnlockNameCardNotify`, `UnlockPersonalLineReq`, `WatcherChangeNotify`, `WearTpsEquipReq`, `WearTpsEquipRsp`, `WidgetDoBagRsp`, `WorldPlayerReviveRsp`, `_TakeAllForgeOutputReq`

## Fields with no 7.2 number (290 in 95 messages)

Parked at field 20000 and up, so they are neither read nor written on the wire. `tools/protocol/remap/recover_lost_72.py` already recovered the fields whose 7.1 and 7.2 names are left unmatched together in at least two messages.

- `AbilityIdentifier`: NBHCDKPBBOI, DLLKGHINFBJ
- `AbilityMetaModifierChange`: MFBDIICANOF, NBBGHMGOALL, PKJMOLPCEBG, OCAMKBCONED
- `AbilityMetaUpdateMoonOvergrowValue`: FOMPMBNENPH
- `AbilityMetaUpdateTpsWeaponAmmunition`: FENKFADBDID
- `ActivityInfo`: JJFJONCHEIK, meet_cond_list, expire_cond_list, is_starting, OPPDJKGIKBJ, DIEFHMGFNIJ, is_common_content_closed, is_play_open_anim
- `ActivityTakeWatcherRewardReq`: activity_id, watcher_id
- `AddNoGachaAvatarCardNotify`: initial_level, initial_promote_level
- `AiSyncInfo`: COFJBBPPANA, MBNNMNHEDAB
- `AttackResult`: resolved_dir, DNMDKFFCNPJ, OHPEIAGBOIK, EFBFOAOJADK, PGPBACPJGNM, OOFNGENKHKG, FDEGAMMGNGI, CFKHDFPCHCL, EKDNPKHMJIE, MDBPICGKMLB, LNFCPOINHKK, IFHJIFEBKAO, EPLABAAGCKE, DMIEAHDCHFA, NJFMEBDIICN, IDENIGMCBJB, PKOFLCBFEOA, OMJGKPMPFBI, GGAMPLOEPMG, PLNPCMILPPD, NILAPFIGLLH, MHJDKCODNAA, MDECMKGHDAK, GMNALDFDANK, JCPDJCGGJCC, BJHAEPGOAKM
- `AvatarEnterSceneInfo`: tps_weapon_list
- `AvatarExpeditionStartRsp`: ILBIKNLHAKE
- `AvatarInfo`: tps_weapon_list
- `BargainSnapshot`: PGNJBNJCBLP, FLCAIJFGLNP
- `BattlePassProduct`: LGOGOFAFJBK, extra_product_id
- `BattlePassSchedule`: JIPEKMKKHOI, cur_cycle_points, paid_platform_flags
- `CancelCoopTaskRsp`: chapter_id
- `CardProductRewardNotify`: OABOKBGPEHG
- `ChangeAvatarReq`: is_move, FGGPBEPIJOG
- `ChapterStateNotify`: MLBMNAKEEOM, EHKKFCAGELK
- `ChatInfo`: ALKIPKCAPFL, OIEHJMHHCFB
- `ClientSetGameTimeRsp`: client_game_time
- `CodexDataUpdateNotify`: OBEHJKBNDJG
- `CombineRsp`: DAPJDPKECGP, GFDAOGHDHCI, FIHLCAAADMG, FJOLFDOBCIL, FBMBEJKABDH
- `CompoundQueueData`: wait_count, output_time, output_count
- `CoopChapter`: ODECGJCJBAB, JKAGGEGDKOH, lock_reason_list
- `DailyDungeonEntryInfo`: ODDLACNLJOF, EDEOIILGGJC, AKNPDDFHEKO
- `DelBackupAvatarTeamRsp`: retcode
- `DoGachaRsp`: is_epitomized
- `DungeonEntryInfo`: max_boss_chest_num, boss_chest_num
- `DungeonPlayerDieNotify`: murderer_entity_id, OBOKABMLGHI, AOMDIHGJMNK
- `EnterSceneDoneRsp`: PPKIKFGEINK
- `EntityFightPropChangeReasonNotify`: EFIBMPKIJJH
- `EvtAvatarEnterFocusNotify`: MEBKKKCNDAK, PNELCGBOIFM, KLNNOPGOMKN, IMMFFEBPCJM, CCDMIMGNCBM, JLNKDGGPICE, ADMFCLBHILF, KHOIOJCHLNL, FDCDPKDKNAL, HFFPEFBLBPC, JHJBHFINKDK, CFNHGMLGPHP
- `EvtBeingHealedNotify`: GOKLCPHOLGL, real_heal_amount, CMCCNKMGKHG, heal_amount
- `EvtBulletHitNotify`: single_bullet_id, hit_entity_id
- `EvtBulletMoveNotify`: EOCBPPKDLBH
- `EvtCreateGadgetNotify`: AAMABJDMIFN, NHNFGNMNDDE, LMLMALLLGBB, PBLACGNNCDF
- `ForgeQueueData`: next_finish_timestamp, unfinish_count, queue_id, total_finish_timestamp
- `ForgeQueueManipulateRsp`: return_item_list, output_item_list
- `FurnitureMakeRsp`: NDADFCDJOBK
- `GadgetInteractReq`: DOGHJJCHDLF, IKMNHOHNBKJ, LPCDPGNKGFL, IKDIFBEOEKM, HDJABIODGCI, EBEELPIEKEL, ALLPOOBNNPH
- `GetAllMailResultNotify`: total_page_count, page_index
- `GetGachaInfoRsp`: BKMCHIJHJJE, LHBELFNPPNK
- `GetPlayerTokenReq`: BIIJMKDGAGI, _gate_ticket, account_type, sub_channel_id
- `GetPlayerTokenRsp`: gm_uid, reg_platform, MGNAAEGCEDL, KONAINKDJCN, CGPIOKGGDPB, is_proficient_player, account_type, sub_channel_id
- `GetScenePointRsp`: JHHBKAOPBAH, IEMCNADNPED, OLEEDBKCAAN
- `GetShopmallDataRsp`: DPHCDGFOLCN
- `HitCollision`: OMEMJMGDNPP, EGIAGADINML, PFLEHLDGODB
- `HomeBlockArrangementInfo`: deploy_furniure_list, persistent_furniture_list
- `HomeMarkPointSceneData`: teapot_spirit_pos, safe_point_pos
- `HomeSceneArrangementInfo`: stair_list, MEEACPBAEJN, IILKHACKPAI, door_list, block_place_pos_list, OADJEMPMKIF, KGGKIAHPDGO
- `IceCreatureAllGadgetInfoNotify`: ice_creature_gadget_info_list
- `IceCreatureBuildReq`: global_value_list, unk_11, build_detail, replace_guid_list
- `IceCreatureBuildRsp`: ice_creature_gadget_info
- `IceCreatureGadgetRemoveReq`: unk_15
- `InvestigationMonsterUpdateNotify`: JDEPBCMJPOP
- `ItemAddHintNotify`: CLKIDLBMBAM, BAIIJPFGOKI, JAKPAGGBINC, OLBMJDOKNAC
- `MarkTargetInvestigationMonsterNotify`: GHPPPJJLDJN
- `MassiveEntityElementOpBatchNotify`: user_id, op_idx, source_element_type, reaction_source_type
- `MusicGameActivityDetailInfo`: NCCMPNEBJLA, EPEEHJNIPNB
- `OneoffGatherPointDetectorData`: CBENILCDDCB
- `ParentQuest`: GFFBOKNBJLC, HONCJCKFDJE, EODCGKGCGDN, DDHKDIOKFIF
- `PersonalLineAllDataRsp`: ongoing_personal_line_list, can_be_unlocked_personal_line_list, NIPKCPGAMKF
- `PingReq`: BLOPFLFBDAE, MCCPJDKDNMJ
- `PlayerApplyEnterMpNotify`: src_thread_index, src_app_id
- `PlayerEnterDungeonReq`: BAEJLCIOPMI, CCIOMCLOGDK
- `PlayerEnterSceneNotify`: PCBFOAKLMKG, ALLIIBMKFLN, HBCAHGNHINM, BGICEPHCGEO
- `PlayerLocationInfo`: HPOIENCPHFN
- `PlayerLoginRsp`: stop_server_info, KKNBKGBLHDM, DPPKDPCKANP, is_sc_open, is_use_ability_hash, GICKBBGJGCK, JBEGCLMHOPF, KLKDKMBAPGA, GHAJDNKLEDA, OEMHPIKBDMB, HACEPFEEKGA
- `PlayerOfferingData`: taken_random_level_reward_map
- `PlayerWorldSceneInfoListNotify`: IIFJABJJJBF
- `ProudSkillExtraLevelNotify`: talent_index, talent_type
- `QueryCodexMonsterBeKilledNumRsp`: GOIJKJBPKAF, BMCNAMPBGAF
- `QueryPathReq`: GEAKGEBKLGB, CPMFACKILJN
- `QueryPathRsp`: INAAJKIFKIF, FBLCJIHADLH
- `QuestTransmitReq`: NCEIBFLFIPM
- `ReliquaryUpgradeRsp`: old_append_prop_list, cur_append_prop_list
- `SceneAvatarInfo`: tps_weapon_list
- `SceneTeamAvatar`: tps_weapon_list
- `Shop`: city_reputation_level, hogdkcckgap
- `ShopGoods`: MEODDILKAAD, ELPGDNACFOA, mcoin, OCFMGIPGLDK, scoin, HBFOJABHIND, AJCBLGEDLFK, single_limit, NGPHJJIEDEH, MMMFLNHEDNI, LDCJFLOPNMP, AIALJDFIEAI, EKBHAAKJINF, KAGKMENGAFH, MBMIKDBJNAN, DCMBDJMCGGP
- `StoreWeightLimitNotify`: CDHAIJGOKFC, DKIEKFJMMIH, OAJHCKBKFPP, reliquary_count_limit, KADABCKKMGA, _beyond_hall_facility_count_limit
- `TakeFurnitureMakeRsp`: return_item_list, output_item_list
- `TeamEnterSceneInfo`: JFAHOBMFLFE, FOMPMBNENPH
- `TowerAllDataRsp`: commemorative_reward_id, EFABCDDNIFA, valid_tower_record_num, skip_to_floor_index, GKIFECKDHKD
- `TowerCurLevelRecord`: cur_level_index, cur_floor_id
- `TowerLevelEndNotify`: continue_state, next_floor_id
- `TowerLevelStarCondData`: cond_value, star_cond_index
- `TpsAmmunitionChange`: ammunition_config_id
- `TrialAvatarActivityDetailInfo`: CBPJGGEBMBI
- `TrialAvatarActivityRewardDetailInfo`: _passed_training_step_list, _available_training_step_list, is_can_take_reward, _is_avatar_owned, passed_dungeon, _is_skip_training, received_reward
- `UgcMusicBriefInfo`: CCJOGFPEKID, COAEPKLMKPE, self_max_score, HHBOBIJKFPJ, NLCMMJILBKJ, IIPJKAOBBBD, MOIDOMCHPFG, note_count, CDEMBFGJAPH
- `WeaponPromoteRsp`: old_promote_level, cur_promote_level
- `WorldPlayerDieNotify`: OBOKABMLGHI, murderer_entity_id, AOMDIHGJMNK
- `_TakeAllForgeOutputRsp`: output_item_list, return_item_list

### Unconfirmed candidates

Pairs where a message has exactly one unmatched 7.1 field and one unmatched 7.2 field of the same type (`recover_lost_72.py --single`). Not applied: check each against a capture first.

- AbilityMetaModifierChange.MFBDIICANOF: 20000 -> 2  (7.1 LFGDCBHDFIL = 7.2 JHCJKEDLDAD, single-message)
- AbilityMetaModifierChange.OCAMKBCONED: 20003 -> 4  (7.1 GMEPMCLHEDP = 7.2 HKFKIALDHIO, single-message)
- AbilityMetaUpdateMoonOvergrowValue.FOMPMBNENPH: 20000 -> 11  (7.1 GCNOLPNPHPA = 7.2 value, single-message)
- TpsAmmunitionChange.ammunition_config_id: 20000 -> 6  (7.1 PPFJCENMNEO = 7.2 item_id, single-message)
- AbilityMetaUpdateTpsWeaponAmmunition.FENKFADBDID: 20000 -> 1  (7.1 FENKFADBDID = 7.2 AEIMHKMPHOG, single-message)
- TrialAvatarActivityDetailInfo.CBPJGGEBMBI: 20000 -> 6  (7.1 MMHPPBIEGFG = 7.2 OIEPLMDMEAM, single-message)
- OneoffGatherPointDetectorData.CBENILCDDCB: 20000 -> 1  (7.1 HDCCPLEDJMH = 7.2 CDLEKEGELMD, single-message)
- AvatarInfo.tps_weapon_list: 20000 -> 37  (7.1 MJGFMDADPJE = 7.2 weapon_list, single-message)
- SceneAvatarInfo.tps_weapon_list: 20000 -> 31  (7.1 MJGFMDADPJE = 7.2 weapon_list, single-message)
- AvatarEnterSceneInfo.tps_weapon_list: 20000 -> 12  (7.1 LMAIAOIPHJL = 7.2 weapon_list, single-message)
- AvatarExpeditionStartRsp.ILBIKNLHAKE: 20000 -> 12  (7.1 KODELLHACAM = 7.2 NEFAGAOLLPG, single-message)
- BattlePassSchedule.JIPEKMKKHOI: 20000 -> 8  (7.1 DFDNNGJHJIC = 7.2 AMBABCAPMGE, single-message)
- ShopGoods.KAGKMENGAFH: 20013 -> 1055  (7.1 AEHELDJPPPH = 7.2 LAJHLJGOMPH, single-message)
- CardProductRewardNotify.OABOKBGPEHG: 20000 -> 8  (7.1 DDOINBPHJMP = 7.2 FOFGCGMLPIB, single-message)
- ChatInfo.ALKIPKCAPFL: 20000 -> 4  (7.1 LOEJDBPKKNF = 7.2 MIBOFEIBECN, single-message)
- ChatInfo.OIEHJMHHCFB: 20001 -> 10  (7.1 IEEJEEEHOGA = 7.2 CBPIPDFPBHN, single-message)
- CodexDataUpdateNotify.OBEHJKBNDJG: 20000 -> 2  (7.1 NELOMLLAKME = 7.2 KBGFCJCMNFP, single-message)
- DailyDungeonEntryInfo.ODDLACNLJOF: 20000 -> 1  (7.1 HAOOOFEBAMI = 7.2 BMOKOMEOKCP, single-message)
- EnterSceneDoneRsp.PPKIKFGEINK: 20000 -> 12  (7.1 OGCIHDJAHHL = 7.2 MFLHLEMFIGI, single-message)
- EntityFightPropChangeReasonNotify.EFIBMPKIJJH: 20000 -> 3  (7.1 HNBCFDELLDN = 7.2 GCFBFOHHHJJ, single-message)
- EvtAvatarEnterFocusNotify.IMMFFEBPCJM: 20003 -> 1  (7.1 NOLEPJHLGKL = 7.2 IGLKLDNNDND, single-message)
- EvtBeingHealedNotify.CMCCNKMGKHG: 20002 -> 13  (7.1 AFAHAOFIJJO = 7.2 is_mute, single-message)
- EvtBeingHealedNotify.GOKLCPHOLGL: 20000 -> 2  (7.1 JONDCAMLJOF = 7.2 heal_tag, single-message)
- EvtBulletMoveNotify.EOCBPPKDLBH: 20000 -> 14  (7.1 JONDPALHPCF = 7.2 NGLAODHCNHB, single-message)
- ParentQuest.DDHKDIOKFIF: 20003 -> 4  (7.1 DHMHGDGIMAD = 7.2 JCHFKGKOJPH, single-message)
- ParentQuest.GFFBOKNBJLC: 20000 -> 7  (7.1 PAJJMIMGHHI = 7.2 KANNCEMKHIG, single-message)
- FurnitureMakeRsp.NDADFCDJOBK: 20000 -> 11  (7.1 IKCDMDEPMDC = 7.2 OEGCONFIHLI, single-message)
- IceCreatureAllGadgetInfoNotify.ice_creature_gadget_info_list: 20000 -> 10  (7.1 _ice_creature_gadget_info_list = 7.2 _ice_creature_info_list, single-message)
- IceCreatureBuildReq.global_value_list: 20001 -> 4  (7.1 LNLLJKPOIOL = 7.2 FLJBADCNDMB, single-message)
- IceCreatureBuildReq.unk_11: 20002 -> 12  (7.1 IFBHPHJDCEO = 7.2 JCNJDMFCMHM, single-message)
- IceCreatureGadgetRemoveReq.unk_15: 20000 -> 11  (7.1 IIMAFOBOEMA = 7.2 FCBLDNACNJK, single-message)
- InvestigationMonsterUpdateNotify.JDEPBCMJPOP: 20000 -> 4  (7.1 GAKDKMHBFGA = 7.2 ICDBOBCNCMB, single-message)
- MarkTargetInvestigationMonsterNotify.GHPPPJJLDJN: 20000 -> 5  (7.1 ILJJLKDMFIK = 7.2 JNLDEIPGDPI, single-message)
- TeamEnterSceneInfo.FOMPMBNENPH: 20002 -> 9  (7.1 GCNOLPNPHPA = 7.2 AGJIEAHJPJC, single-message)
- PlayerEnterSceneNotify.ALLIIBMKFLN: 20001 -> 763  (7.1 NJEOMOIDMNM = 7.2 LFPEKBJIJNG, single-message)
- PlayerEnterSceneNotify.BGICEPHCGEO: 20003 -> 46  (7.1 NJDJMFJCMBM = 7.2 LMKLCDPJOBJ, single-message)
- PlayerLocationInfo.HPOIENCPHFN: 20000 -> 4  (7.1 IEPPHCJANGA = 7.2 HFFCDKKCIMN, single-message)
- PlayerLoginRsp.KKNBKGBLHDM: 20002 -> 265  (7.1 HMMPPDIBFAO = 7.2 HIHJANHKAMO, single-message)
- PlayerLoginRsp.stop_server_info: 20000 -> 1314  (7.1 BMCDKDDKIBH = 7.2 LBDHEONCLNL, single-message)
- PlayerOfferingData.taken_random_level_reward_map: 20000 -> 2  (7.1 PKOKLIFNFIB = 7.2 CBMCLHIIEKD, single-message)
- PlayerWorldSceneInfoListNotify.IIFJABJJJBF: 20000 -> 693  (7.1 FMIPEDBGOMN = 7.2 unlocked_area_id_list, single-message)
- QueryPathRsp.FBLCJIHADLH: 20001 -> 3  (7.1 MLPCGBFDBGC = 7.2 OCOOOCJJPCC, single-message)
- QueryPathRsp.INAAJKIFKIF: 20000 -> 10  (7.1 ANBCOBHEMFO = 7.2 EGDBEDNAFGL, single-message)
- QuestTransmitReq.NCEIBFLFIPM: 20000 -> 13  (7.1 EOPPLJLCEIJ = 7.2 HEJLIMPMKLL, single-message)
- SceneTeamAvatar.tps_weapon_list: 20000 -> 645  (7.1 LMAIAOIPHJL = 7.2 weapon_list, single-message)

## Handwritten parsers

- `net/proto/AvatarPromoteReqParser.java` reads the ascension request guid from field 3, the 7.1 client field. `AvatarPromoteReq` has no named 7.2 counterpart, so the 7.2 field is unknown.

## Handwritten wire code still on 7.1 numbers

These build or read protobuf bytes by hand with 7.1 field numbers or CmdIds and need checking against 7.2 captures:

- `emu/grasscutter/game/entity/GameEntity.java`
- `emu/grasscutter/game/inventory/ReliquaryProtoCompat.java`
- `emu/grasscutter/game/player/BeyondProfilePictureWire.java`
- `emu/grasscutter/game/player/PushTipsSuppressHelper.java`
- `emu/grasscutter/net/proto/AvatarExtraLevelUpgradeReqParser.java`
- `emu/grasscutter/server/packet/recv/HandlerBattlePassSetRewardPlanReq70.java`
- `emu/grasscutter/server/packet/send/PacketBeyondCreateHallRsp.java`
- `emu/grasscutter/server/packet/send/PacketBeyondFull.java`
- `emu/grasscutter/server/packet/send/PacketBeyondPlayerDetailRsp.java`
- `emu/grasscutter/server/packet/send/PacketBeyondProfilePictureDataNotify.java`
- `emu/grasscutter/server/packet/send/PacketDoGachaRsp.java`
- `emu/grasscutter/server/packet/send/PacketGetBeyondPlayerInfoRsp.java`
- `emu/grasscutter/server/packet/send/PacketGetProfilePictureDataRsp.java`
- `emu/grasscutter/server/packet/send/PacketListProbe.java`
- `emu/grasscutter/server/packet/send/PacketPlayerNicknameNotify.java`
- `emu/grasscutter/server/packet/send/PacketSeeMonsterRsp.java`
- `emu/grasscutter/server/packet/send/PacketSetPlayerBornDataRsp.java`
- `emu/grasscutter/server/packet/send/PacketTeamHexenzirkelChangeNotify.java`
- `emu/grasscutter/server/packet/send/PacketTowerCurLevelRecordChangeNotify.java`
- `emu/grasscutter/server/packet/send/PacketWindSeedClientNotify.java`
- `emu/grasscutter/utils/ProtoWire.java`
