/******************************************************************
 *
 *
 *		----------------
 *		   Game.cpp
 *		----------------
 *
 *		Classe Game
 *
 *		La classe représentant une partie
 *
 *
 *		Prosper / LOADED -   V 0.2
 *
 *
 *
 ******************************************************************/

#define GAME_CPP_FILE

#include <algorithm>

#include <fcntl.h>
#include <malloc.h>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "ben_debug.h"
#include "ben_divers.h"
#include "blip.h"
#include "blop.h"
#include "bonus.h"
#include "bulle.h"
#include "character_selection.h"
#include "cine_player.h"
#include "config.h"
#include "dd_gfx.h"
#include "enemy.h"
#include "event_bonus.h"
#include "event_ennemi.h"
#include "event_fond_anime.h"
#include "event_gen_bonus.h"
#include "event_gen_ennemi.h"
#include "event_hold_fire.h"
#include "event_lock.h"
#include "event_meteo.h"
#include "event_mi_fond.h"
#include "event_music.h"
#include "event_premier_plan.h"
#include "event_rpg.h"
#include "event_scroll_speed.h"
#include "event_set_flag.h"
#include "event_son.h"
#include "event_texte.h"
#include "event_vehicule.h"
#include "explosion.h"
#include "fic_events.h"
#include "game.h"
#include "gen_bonus.h"
#include "gen_ennemi.h"
#include "globals.h"
#include "input.h"
#include "key_translator.h"
#include "lgx_packer.h"
#include "make_bonus.h"
#include "menu_game.h"
#include "menu_main.h"
#include "meteo_neige.h"
#include "meteo_pluie.h"
#include "restore.h"
#include "scroll.h"
#include "texte_cool.h"
#include "tir_bb.h"
#include "tir_bb_vache.h"
#include "txt_data.h"
#include "vehicule.h"

#include "precache.h"
#include "trace.h"

Personnage dummyPlayer;

//-----------------------------------------------------------------------------

#define TXT_TIME 828

//-----------------------------------------------------------------------------

Game::Game()
    : player1(NULL),
      player2(NULL),
      next_goutte(0),
      next_flocon(0),
      show_fps(false),
      show_lists(false) {
    briefing = false;
}

//-----------------------------------------------------------------------------

void Game::jouePartie(int nbj, int idj) {
    if (app_killed) return;

    CINEPlayer cine;
    bool letsgo = true;
    int i;

    // Bidon
    //
    mbk_interl.stop();

    // Charge Tout
    //
    if (!chargePartie()) {
        releasePartie();
        return;
    }

    wait_for_death = 0;
    current_zik = -1;

    if (idj == 0)
        player1 = new Blip();
    else
        player1 = new Blop();

    player1->ctrl = &ctrlP1;
    list_joueurs.push_back(player1);

    if (nbj == 2) {
        if (idj == 0)
            player2 = new Blop();
        else
            player2 = new Blip();

        player2->ctrl = &ctrlP2;
        list_joueurs.push_back(player2);
    }

    // Joue à tous les niveaux
    //
    cowBombOn = false;
    last_perfect1 = last_perfect2 = false;
    int nbNiv = 0;
    i = 0;

    while (i < nb_part && letsgo && !app_killed) {
        if (type_part[i] == PART_LEVEL) {
            letsgo = joueNiveau(fic_names[i], type_lvl[i]);

            if (!cowBombOn) {
                if (player1 != NULL) player1->nb_cow_bomb = 1;

                if (player2 != NULL) player2->nb_cow_bomb = 1;

                cowBombOn = true;
            }
        } else if (type_part[i] == PART_BRIEFING) {
            showBriefing(fic_names[i]);
        } else {
            cine.playScene(fic_names[i], primSurface, backSurface);
        }

        i += 1;
    }

    if (letsgo && !app_killed) {
        MusicBank mbk;
        MusicBank mbk2;
        PictureBank pbk;

        pbk.loadGFX("data/theend.gfx", DDSURF_BEST, false);
        mbk.open("data/credits.mbk");
        mbk2.open("data/end.mbk", false);
        cine.loadPBK("data/end.gfx");
        mbk.play(1);
        cine.playScene("data/end.cin", primSurface, backSurface);
        mbk.stop();

        pbk[1]->PasteTo(backSurface, 0, 0);
        DDFlip();
        pbk[1]->PasteTo(backSurface, 0, 0);
        DDFlip();

        if (!in.anyKeyPressed()) mbk2.play(0);

        in.waitClean();
        in.waitKey();
        in.waitClean();
        mbk2.stop();

        mbk.play(0);
        showCredits(true);

        pbk[0]->PasteTo(backSurface, 0, 0);
        DDFlip();
        pbk[0]->PasteTo(backSurface, 0, 0);
        DDFlip();

        in.waitClean();
        in.waitKey();
        in.waitClean();

        mbk.stop();

        showPE(false);
    }

    // Désalloue tout
    //
    releasePartie();

    pbk_inter.loadGFX("data/inter.gfx", DDSURF_BEST);

    if (!skipped && !app_killed) {
        if (!letsgo) {
            showGameOver();
        }

        getHiscore();
        showHighScores();
    }

    if (player1 != NULL) {
        delete player1;
        player1 = NULL;
    }

    if (player2 != NULL) {
        delete player2;
        player2 = NULL;
    }
}

//-----------------------------------------------------------------------------

bool Game::joueNiveau(const char* nom_niveau, int type) {
    int p1_life;  // Si il s'agit d'un niveau bonus on
    bool p1_bringBack;
    int p2_life;  // conserve le nombre de vies des joueurs
    bool p2_bringBack;

    // Little init
    //
    joueurs_morts = false;
    niveau_fini = false;
    skipped = false;
    wait_for_victory = -1;

    etape_timer = 0;
    go_.Reset();

    scroll_locked = false;
    scroll_speed = 0;
    no_scroll1 = false;
    no_scroll2 = false;

    hold_fire = false;

    intensite_meteo = 0;
    dy_tremblement = 0;
    etape_tremblement = 0;
    amplitude_tremblement = 0;

    n_cache = 0;

    for (int i = 0; i <= 10; i++) game_flag[i] = 0;

    clearTexteCool();

    if (!chargeNiveau(nom_niveau)) return false;

    if (strcmp(nom_niveau, "data/snorkniv.lvl") == 0 ||
        strcmp(nom_niveau, "data/snorkniv2.lvl") == 0) {
        okLanceFlame = false;
    } else {
        okLanceFlame = true;
    }

    // Place les joueurs et initialise qq trucs
    //
    if (player1 != NULL && player1->nb_life > 0) {
        player1->x = xstart1;
        player1->y = ystart1;
        player1->etat = ETAT_NORMAL;
        player1->etape = 0;
        player1->ss_etape = 0;
        player1->dir = BBDIR_DROITE;
        player1->tire = false;

        if (type == LVL_BONUS) {
            p1_life = player1->nb_life;
            p1_bringBack = true;

            if (p1_life > 0) player1->nb_life = 1;
        }
    } else {
        p1_bringBack = false;
    }

    if (player2 != NULL && player2->nb_life > 0) {
        player2->x = xstart2;
        player2->y = ystart2;
        player2->etat = ETAT_NORMAL;
        player1->etape = 0;
        player1->ss_etape = 0;
        player2->dir = BBDIR_DROITE;
        player2->tire = false;

        if (type == LVL_BONUS) {
            p2_life = player2->nb_life;
            p2_bringBack = true;

            if (p2_life > 0) player2->nb_life = 1;
        }
    } else {
        p2_bringBack = false;
    }

    // Règlages conditionnels
    //
    if (type == LVL_BONUS || type == LVL_COMPLETE || type == LVL_FIRST ||
        type == LVL_END) {
        nb_ennemis_created = 0;

        if (player1 != NULL) {
            player1->setKilled(0);

            if (type != LVL_END) {
                player1->etat = ETAT_COME_BACK;
                player1->y = -50;
                player1->y_to_go = ystart1;
            }

            player1->id_arme = ID_M16;
            player1->latence_arme = 3;
            player1->nb_etape_arme = 5;
            player1->cadence_arme = 10;
            player1->poid_arme = 1;
            player1->perfect = true;
            player1->pv = 5;

            if (last_perfect1) {
                player1->setSuperWeapon();
            }
        }

        if (player2 != NULL) {
            player2->setKilled(0);

            if (type != LVL_END) {
                player2->etat = ETAT_COME_BACK;
                player2->y = -50;
                player2->y_to_go = ystart2;
            }

            player2->id_arme = ID_M16;
            player2->latence_arme = 3;
            player2->nb_etape_arme = 5;
            player2->cadence_arme = 10;
            player2->poid_arme = 1;
            player2->perfect = true;
            player2->pv = 5;

            if (last_perfect2) {
                player2->setSuperWeapon();
            }
        }
    }

    last_perfect1 = last_perfect2 = false;

    // Niveau bonus = temps limite
    //
    niveau_bonus = type == LVL_BONUS;

    // Règle l'offset sur les joueurs
    //
    /*
            list_joueurs.start();
            Sprite * s = (Sprite*) list_joueurs.info();
            offset = s->x;
            offset -= (offset%640);

            n_img = offset / 640;
            xTex = 0;

            next_x = offset % vbuffer_wide;
            next_x = next_x - (next_x & 1);
    */
    Sprite* s = list_joueurs[0];
    offset = s->x;
    offset -= (offset % 640);

    n_img = 0;
    xTex = 0;
    n_cache = 0;
    next_x = 0;

    debug
        << "---------------------------------------------------------------\n";
    debug << "Now getting into game loop.\n";
    debug
        << "---------------------------------------------------------------\n";

    /*DDSCAPS2		ddscaps2dummy;
    DWORD			vid_mem1;
    DWORD			vid_mem2;

    ZeroMemory(&ddscaps2dummy, sizeof(ddscaps2dummy));
    ddscaps2dummy.dwCaps = DDSCAPS_VIDEOMEMORY;
    graphicInstance->GetAvailableVidMem(&ddscaps2dummy, &vid_mem1, &vid_mem2);

    debug << "Available video memory : " << (vid_mem2 >> 10) << " Ko\n";*/

    SDL_Delay(2000);

    if (briefing) {
        DDBLTFX ddfx;
        Rect r;

        memset(&ddfx, 0, sizeof(ddfx));
        ddfx.dwSize = sizeof(ddfx);
        ddfx.dwFillColor = 0;  // Noir

        r.left = 0;
        r.right = 640;
        r.top = 0;
        r.bottom = 480;

        // backSurface->Blt(&r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL,
        // &ddfx);
        backSurface->FillRect(&r, 0);

        pbk_briefing[0]->PasteTo(backSurface, 0, 0);
        fnt_rpg.printC(backSurface, 320, 460, "Press a key to start.");
        DDFlip();

        mbk_inter.play(2);
        in.waitClean();
        in.waitKey();
        briefing = false;
    }

    // Update quelques trucs
    //
    updateEvents();
    updateAll();

    // Pour le mode automatique
    // FIXME: what is this auto mode? should that step really be there?
    update_regulator_.Skip();

    // Boucle principale
    //
    while (!joueurs_morts && !niveau_fini && !skipped && !app_killed) {
        gameLoop();
        updateVictoryAndDefeat();
    }

    if (joueurs_morts) debug << "Players died\n";
    if (niveau_fini) debug << "Level complete\n";
    if (skipped) debug << "User skipped\n";
    if (app_killed) debug << "Application killed\n";

    if (player1 != NULL) {
        player1->endLevel();
    }

    if (player2 != NULL) {
        player2->endLevel();
    }

    // Coupe les musiques
    //
    current_zik = -1;
    if (music_on) mbk_niveau.stop();

    // S'il s'agissait d'un niveau bonus on restaure les vies des
    // joueurs + on ne peut pas perdre
    //
    if (type == LVL_BONUS && !skipped && !app_killed) {
        if (player1 != NULL && p1_bringBack) {
            if (player1->a_detruire && p1_life > 0) {
                player1->a_detruire = false;
                list_joueurs.push_back(player1);
            }

            player1->nb_life = p1_life;
        }

        if (player2 != NULL && p2_bringBack) {
            if (player2->a_detruire && p2_life > 0) {
                player2->a_detruire = false;
                list_joueurs.push_back(player2);
            }

            player2->nb_life = p2_life;
        }

        niveau_fini = true;
    }

    // Récapitulatif de fin de niveau
    //
    if (niveau_fini && !joueurs_morts && type != LVL_END) {
        drawAll(false);
        DDFlip();
        drawAll(false);

        if ((type == LVL_BONUS && !skipped && game_flag[FLAG_TIMER] > 0) ||
            type == LVL_COMPLETE || type == LVL_LAST) {
            if (player1 != NULL) {
                last_perfect1 = player1->perfect;
            }

            if (player2 != NULL) {
                last_perfect2 = player2->perfect;
            }

            showPE((type == LVL_BONUS));
        } else if (type == LVL_BONUS) {
            showPE(true, true);
        }
    } else if (type == LVL_BONUS) {
        showPE(true, true);
    }

    drawAll(false);
    drawLoading();
    DDFlipV();

    // Libére la mémoire
    //
    releaseNiveau();

    return niveau_fini;
}

//-----------------------------------------------------------------------------

bool Game::chargeNiveau(const char* nom_niveau) {
    char buffer[20];
    char buffer2[70];

    std::ifstream fic(nom_niveau, std::ios::binary);

    if (!fic.good()) {
        debug << "Game::chargeNiveau() -> Cannot load <" << nom_niveau << ">\n";
        return false;
    }

    debug
        << "---------------------------------------------------------------\n";
    debug << "Loading level <" << nom_niveau << ">\n";
    debug
        << "---------------------------------------------------------------\n";

    // GFX decors
    //
    fic.read(buffer, 20);
    strcpy(buffer2, "data/");
    strcat(buffer2, buffer);

    if (!pbk_decor.loadGFX(buffer2, DDSURF_SYSTEM)) {
        debug << "Game::chargeNiveau() -> Cannot load " << buffer2
              << " as background\n";
        return false;
    }

    debug << "Successfully loaded <" << buffer2 << "> as background\n";

    // GFX niveau (fonds animés & co)
    //
    fic.read(buffer, 20);
    if (strlen(buffer) != 0) {
        strcpy(buffer2, "data/");
        strcat(buffer2, buffer);

        if (!pbk_niveau.loadGFX(buffer2, mem_flag)) {
            debug << "Game::chargeNiveau() -> Cannot load " << buffer2
                  << " as level stuff\n";
            return false;
        }

        debug << "Successfully loaded <" << buffer2 << "> as level stuff\n";
    }

    // GFX ennemis
    //
    fic.read(buffer, 20);
    if (strlen(buffer) != 0) {
        strcpy(buffer2, "data/");
        strcat(buffer2, buffer);

        if (!pbk_ennemis.loadGFX(buffer2, mem_flag)) {
            debug << "Game::chargeNiveau() -> Cannot load " << buffer2
                  << " as ennemies\n";
            return false;
        }

        debug << "Successfully loaded <" << buffer2 << "> as ennemies\n";
    }

    // SBK ennemis
    //
    fic.read(buffer, 20);

    if (strlen(buffer) != 0) {
        strcpy(buffer2, "data/");
        strcat(buffer2, buffer);

        if (!sbk_niveau.loadSFX(buffer2)) {
            debug << "Game::chargeNiveau() -> Cannot load " << buffer2
                  << " as SBK\n";
            return false;
        }

        debug << "Successfully loaded <" << buffer2 << "> as SBK\n";
    }

    // Fichier MBK
    //
    fic.read(buffer, 20);
    if (strlen(buffer) != 0) {
        strcpy(buffer2, "data/");
        strcat(buffer2, buffer);
        strcpy(current_mbk, buffer2);

        if (music_on) {
            if (!mbk_niveau.open(buffer2)) {
                debug << "Game::chargeNiveau() -> Cannot load " << buffer2
                      << " as MKB\n";
                return false;
            }
            debug << "Successfully loaded <" << buffer2 << "> as MBK\n";
        }
    } else
        strcpy(current_mbk, "");

    // Fichier RPG itself
    //
    fic.read(buffer, 20);
    if (strlen(buffer) != 0) {
        strcpy(buffer2, "data/");
        strcat(buffer2, buffer);
        rpg.attachFile(buffer2);

        // Precache le fichier RPG
        //
        Precache(buffer2);

        debug << "Successfully loaded <" << buffer2 << "> as RPG file\n";
    }

    // GFX rpg
    //
    fic.read(buffer, 20);
    if (strlen(buffer) != 0) {
        strcpy(buffer2, "data/");
        strcat(buffer2, buffer);

        if (!pbk_rpg.loadGFX(buffer2, mem_flag)) {
            debug << "Game::chargeNiveau() -> Cannot load " << buffer2
                  << " as RPG GFX\n";
            return false;
        }

        debug << "Successfully loaded <" << buffer2 << "> as RPG GFX\n";
    }

    // Taille du niveau
    //
    fic.read(reinterpret_cast<char*>(&scr_level_size), sizeof(scr_level_size));
    level_size = scr_level_size * 640;

    // Numéros des écrans à afficher (comme des tiles)
    //
    num_decor = new int[scr_level_size];
    for (int i = 0; i < scr_level_size; i++)
        fic.read(reinterpret_cast<char*>(&num_decor[i]), sizeof(int));

    // Coordonnées de départ des joueurs
    //
    fic.read(reinterpret_cast<char*>(&xstart1), sizeof(xstart1));
    fic.read(reinterpret_cast<char*>(&ystart1), sizeof(ystart1));
    fic.read(reinterpret_cast<char*>(&xstart2), sizeof(xstart2));
    fic.read(reinterpret_cast<char*>(&ystart2), sizeof(ystart2));

    // Conditions de victoire
    //
    fic.read(reinterpret_cast<char*>(&vic_x), sizeof(vic_x));
    fic.read(reinterpret_cast<char*>(&vic_flag1), sizeof(vic_flag1));
    fic.read(reinterpret_cast<char*>(&vic_val1), sizeof(vic_val1));
    fic.read(reinterpret_cast<char*>(&vic_flag2), sizeof(vic_flag2));
    fic.read(reinterpret_cast<char*>(&vic_val2), sizeof(vic_val2));

    //
    // Plateformes
    //
    y_plat = new int*[NB_MAX_PLAT];

    for (int i = 0; i < NB_MAX_PLAT; i++) {
        y_plat[i] = new int[level_size];
        fic.read(reinterpret_cast<char*>(y_plat[i]),
                 (level_size) * sizeof(int));
    }

    //
    // Murs opaques
    //
    int level_size_8 = level_size / 8;
    murs_opaques = new bool*[60];

    for (int i = 0; i < 60; i++) {
        murs_opaques[i] = new bool[level_size_8];
        fic.read(reinterpret_cast<char*>(murs_opaques[i]),
                 (level_size_8) * sizeof(bool));
    }

    //
    // Murs sanglants
    //
    murs_sanglants = new bool*[60];

    for (int i = 0; i < 60; i++) {
        murs_sanglants[i] = new bool[level_size_8];
        fic.read(reinterpret_cast<char*>(murs_sanglants[i]),
                 (level_size_8) * sizeof(bool));
    }

    //
    // Charge les évenements
    //
    FICEVENT ficevent;
    int nb_events;

    fic.read(reinterpret_cast<char*>(&nb_events), sizeof(nb_events));

    for (int i = 0; i < nb_events; i++) {
        fic.read(reinterpret_cast<char*>(&ficevent), sizeof(ficevent));

        switch (ficevent.event_id) {
            case EVENTID_ENNEMI: {
                auto event_ennemi = std::make_unique<EventEnnemi>();

                event_ennemi->x_activation = ficevent.x_activation;
                event_ennemi->id_ennemi = ficevent.id;
                event_ennemi->x = ficevent.x;
                event_ennemi->y = ficevent.y;
                event_ennemi->sens = ficevent.sens;

                list_event_endormis.push_back(std::move(event_ennemi));
                break;
            }

            case EVENTID_ENNEMI_GENERATOR: {
                auto event_gennemi = std::make_unique<EventGenEnnemi>();

                event_gennemi->x_activation = ficevent.x_activation;
                event_gennemi->id_ennemi = ficevent.id;
                event_gennemi->x = ficevent.x;
                event_gennemi->y = ficevent.y;
                event_gennemi->sens = ficevent.sens;
                event_gennemi->capacite = ficevent.capacite;
                event_gennemi->periode = ficevent.periode;
                event_gennemi->tmp = ficevent.tmp;

                list_event_endormis.push_back(std::move(event_gennemi));
                break;
            }

            case EVENTID_LOCK: {
                auto event_lock = std::make_unique<EventLock>();

                event_lock->x_activation = ficevent.x_activation;
                event_lock->cond = ficevent.cond;
                event_lock->flag = ficevent.flag;
                event_lock->val = ficevent.val;

                list_event_endormis.push_back(std::move(event_lock));
                break;
            }

            case EVENTID_FORCE_SCROLL: {
                auto event_scroll_speed = std::make_unique<EventScrollSpeed>();

                event_scroll_speed->x_activation = ficevent.x_activation;
                event_scroll_speed->speed = ficevent.speed;

                list_event_endormis.push_back(std::move(event_scroll_speed));
                break;
            }

            case EVENTID_FLAG: {
                auto event_set_flag = std::make_unique<EventSetFlag>();

                event_set_flag->x_activation = ficevent.x_activation;
                event_set_flag->flag = ficevent.flag;
                event_set_flag->val = ficevent.val;

                list_event_endormis.push_back(std::move(event_set_flag));
                break;
            }

            case EVENTID_HOLD_FIRE: {
                auto event_hold_fire = std::make_unique<EventHoldFire>();

                event_hold_fire->x_activation = ficevent.x_activation;
                event_hold_fire->flag = ficevent.flag;
                event_hold_fire->val = ficevent.val;

                list_event_endormis.push_back(std::move(event_hold_fire));
                break;
            }

            case EVENTID_BONUS_GENERATOR: {
                auto event_gen_bonus = std::make_unique<EventGenBonus>();

                event_gen_bonus->x_activation = ficevent.x_activation;
                event_gen_bonus->type = ficevent.id;
                event_gen_bonus->periode = ficevent.periode;

                list_event_endormis.push_back(std::move(event_gen_bonus));
                break;
            }

            case EVENTID_TEXT: {
                auto event_texte = std::make_unique<EventTexte>();

                event_texte->x_activation = ficevent.x_activation;
                event_texte->ntxt = ficevent.n_txt;
                event_texte->cond = ficevent.cond;
                event_texte->flag = ficevent.flag;
                event_texte->val = ficevent.val;

                list_event_endormis.push_back(std::move(event_texte));
                break;
            }

            case EVENTID_FOND_ANIME: {
                auto event_fond_anime = std::make_unique<EventFondAnime>();

                event_fond_anime->x_activation = ficevent.x_activation;
                event_fond_anime->id_fond = ficevent.id;
                event_fond_anime->x = ficevent.x;
                event_fond_anime->y = ficevent.y;

                list_event_endormis.push_back(std::move(event_fond_anime));
                break;
            }

            case EVENTID_MIFOND: {
                auto event_mi_fond = std::make_unique<EventMiFond>();

                event_mi_fond->x_activation = ficevent.x_activation;
                event_mi_fond->id = ficevent.id;
                event_mi_fond->x = ficevent.x;
                event_mi_fond->y = ficevent.y;

                list_event_endormis.push_back(std::move(event_mi_fond));
                break;
            }

            case EVENTID_PREMIER_PLAN: {
                auto event_pplan = std::make_unique<EventPremierPlan>();

                event_pplan->id_fond = ficevent.id;
                event_pplan->x_activation = ficevent.x_activation;
                event_pplan->x = ficevent.x;
                event_pplan->y = ficevent.y;

                list_event_endormis.push_back(std::move(event_pplan));
                break;
            }

            case EVENTID_RPG: {
                auto event_rpg = std::make_unique<EventRPG>();

                event_rpg->x_activation = ficevent.x_activation;
                event_rpg->num = ficevent.id;
                event_rpg->cond = ficevent.cond;
                event_rpg->flag = ficevent.flag;
                event_rpg->val = ficevent.val;

                list_event_endormis.push_back(std::move(event_rpg));
                break;
            }

            case EVENTID_MUSIC: {
                auto event_music = std::make_unique<EventMusic>();

                event_music->x_activation = ficevent.x_activation;
                event_music->id = ficevent.id;
                event_music->play = ficevent.play;

                list_event_endormis.push_back(std::move(event_music));
                break;
            }

            case EVENTID_METEO: {
                auto event_meteo = std::make_unique<EventMeteo>();

                event_meteo->x_activation = ficevent.x_activation;
                event_meteo->intensite = ficevent.intensite;
                event_meteo->type = ficevent.id;

                list_event_endormis.push_back(std::move(event_meteo));
                break;
            }

            case EVENTID_BONUS: {
                auto event_bonus = std::make_unique<EventBonus>();
                event_bonus->x_activation = ficevent.x_activation;
                event_bonus->type = ficevent.id;
                event_bonus->x = ficevent.x;
                event_bonus->y = ficevent.y;

                list_event_endormis.push_back(std::move(event_bonus));
                break;
            }

            case EVENTID_TURRET: {
                auto event_vehicule = std::make_unique<EventVehicule>();
                event_vehicule->x_activation = ficevent.x_activation;
                event_vehicule->id_vehicule = ficevent.id;
                event_vehicule->x = ficevent.x;
                event_vehicule->y = ficevent.y;
                event_vehicule->dir = ficevent.dir;

                list_event_endormis.push_back(std::move(event_vehicule));
                break;
            }

            case EVENTID_SON: {
                auto event_son = std::make_unique<EventSon>();
                event_son->x_activation = ficevent.x_activation;
                event_son->nsnd = ficevent.id;

                list_event_endormis.push_back(std::move(event_son));
                break;
            }
        }
    }

    debug << "Successfully loaded all level files\n";

    return true;
}

//-----------------------------------------------------------------------------

void Game::releaseNiveau() {
    if (num_decor != NULL) {
        delete[] num_decor;
        num_decor = NULL;
    }

    if (y_plat != NULL) {
        for (int i = 0; i < NB_MAX_PLAT; i++) delete[] y_plat[i];

        delete[] y_plat;
        y_plat = NULL;
    }

    if (murs_sanglants != NULL) {
        for (int i = 0; i < 60; i++) delete[] murs_sanglants[i];

        delete[] murs_sanglants;
        murs_sanglants = NULL;
    }

    if (murs_opaques != NULL) {
        for (int i = 0; i < 60; i++) delete[] murs_opaques[i];

        delete[] murs_opaques;
        murs_opaques = NULL;
    }

    list_event_endormis.clear();
    list_event.clear();

    list_tirs_bb.clear();
    list_cow.clear();
    list_impacts.clear();

    list_vehicules.clear();

    list_ennemis.clear();
    list_tirs_ennemis.clear();
    list_gen_ennemis.clear();

    list_bonus.clear();
    list_gen_bonus.clear();

    list_txt_cool.clear();

    list_fonds_animes.clear();
    list_fonds_statiques.clear();
    list_premiers_plans.clear();
    list_plateformes_mobiles.clear();

    list_giclures.clear();
    list_gore.clear();

    list_meteo.clear();
    list_bulles.clear();
}

//-----------------------------------------------------------------------------

void Game::updateAll() {
    /*
            if ( scroll_speed < 1)
                    scroll_speed = 1;
    */

    manageMsg();  // Fucking windaube!!!

    if (checkRestore()) update_regulator_.Skip();

    phase = !phase;

    if (phase) slow_phase = !slow_phase;

    // Met à jour les touches
    //
    in.update();

    // Ok bonus ?
    //
    okBonus = false;

    if (player1 != NULL) {
        okBonus = okBonus || player1->okBonus();
    }

    if (player2 != NULL) {
        okBonus = okBonus || player2->okBonus();
    }

    // Update tout ce qu'il faut
    //
    if (!no_scroll1 && !no_scroll2) updateScrolling(false);

    updateEvents();

    // On update le scroll une deuxième fois à cause des Locks
    //
    if (!no_scroll1 && !no_scroll2) updateScrolling();

    updateCheat();
    updateTremblements();

    if (game_flag[FLAG_BULLES]) updateBulles();

    if (type_meteo == METEO_PLUIE || type_meteo == METEO_NEIGE) updateMeteo();

    UpdateCollection(list_fonds_statiques);
    UpdateCollection(list_fonds_animes);
    UpdateCollection(list_plateformes_mobiles);
    UpdateCollection(list_vehicules);
    UpdateCollection(list_joueurs);
    UpdateCollection(list_tirs_bb);
    UpdateCollection(list_cow);
    UpdateCollection(list_impacts);
    UpdateCollection(list_ennemis);
    UpdateCollection(list_gore);
    UpdateCollection(list_tirs_ennemis);
    UpdateCollection(list_giclures);
    UpdateCollection(list_bonus);
    UpdateCollection(list_premiers_plans);

    UpdateCollection(list_txt_cool);
    UpdateCollection(list_gen_ennemis);
    UpdateCollection(list_gen_bonus);
    updateLock();
    updateHoldFire();
    updateTeteTurc();
    updateFlags();
    updateFlecheGo();

    if (type_meteo == METEO_DEFORME && intensite_meteo != 0)
        updateDeformation();

    manageCollisions();

    // Retire les trucs inutiles des listes
    //
    cleanLists();

    updateMenu();

    updateRPG();
}

//-----------------------------------------------------------------------------

void Game::drawAll(bool flip) {
    manageMsg();  // Fucking windaube!!!

    if (checkRestore()) update_regulator_.Skip();

    if (!flip) {
        phase = false;
        slow_phase = false;
    }

    fps_current_count += 1;

    /*if (mustFixGforceBug) {
            RECT	r;

            r.top	= 10;
            r.bottom = 150;
            r.left	= 200;
            r.right = 440;

            backSurface->Blt(&r, videoA, NULL, DDBLT_WAIT, 0);
    }*/

    drawScrolling();
    DrawCollection(list_fonds_statiques);
    DrawCollection(list_fonds_animes);

    if (game_flag[FLAG_BULLES]) {
        DrawCollection(list_bulles);
    }

    DrawCollection(list_plateformes_mobiles);
    DrawCollection(list_impacts);
    DrawCollection(list_gore);

    DrawCollection(list_ennemis);
    DrawCollection(list_tirs_ennemis);
    DrawCollection(list_bonus);
    DrawCollection(list_tirs_bb);
    DrawCollection(list_joueurs);
    DrawCollection(list_vehicules);
    DrawCollection(list_impacts);
    DrawCollection(list_cow);
    DrawCollection(list_meteo);
    DrawCollection(list_premiers_plans);

    if (type_meteo == METEO_DEFORME && intensite_meteo != 0) drawDeformation();

    // FIXME: Disable it for now as it works unproperly at least on Linux
    // drawTremblements();
    drawHUB();
    drawTimer();
    go_.Draw();
    DrawCollection(list_txt_cool);

    drawDebugInfos();

    // Virer la bande qui déconne si besoin est
    //
    if (vbuffer_wide == 640) {
        DDBLTFX ddfx;
        Rect r;

        memset(&ddfx, 0, sizeof(ddfx));
        ddfx.dwSize = sizeof(ddfx);
        ddfx.dwFillColor = 0;  // Noir

        r.left = 638;
        r.right = 640;
        r.top = 0;
        r.bottom = 480;

        backSurface
            ->/*Blt(&r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddfx)*/
            FillRect(&r, 0);
    }

    Rect r;

    r.top = 0;
    r.left = 0;
    r.right = 640;
    r.bottom = 480;

    if (flip) {
        DDFlip();
    }
}

//-----------------------------------------------------------------------------

void Game::gameLoop() {
    int n_updates = update_regulator_.Step();
    for (; n_updates > 0; --n_updates) {
        updateAll();
    }

    drawAll();
}

//-----------------------------------------------------------------------------

void Game::releasePartie() { list_joueurs.clear(); }

//-----------------------------------------------------------------------------

bool Game::chargePartie() {
    // Charge les joueurs
    //
    debug
        << "---------------------------------------------------------------\n";
    debug << "Loading global game files...\n";
    debug
        << "---------------------------------------------------------------\n";

    if (!pbk_blip.loadGFX("data/blip.gfx", mem_flag)) return false;

    debug << "Successfully loaded <blip.gfx>\n";

    if (!pbk_blop.loadGFX("data/blop.gfx", mem_flag)) return false;

    debug << "Successfully loaded <blop.gfx>\n";

    // Charge BB
    //
    if (!pbk_bb.loadGFX("data/bb.gfx", mem_flag)) return false;

    debug << "Successfully loaded <bb.gfx>\n";

    // Charge Divers
    //
    if (!pbk_misc.loadGFX("data/misc.gfx", mem_flag)) return false;

    debug << "Successfully loaded <misc.gfx>\n";

    // Charge gueules BB
    //
    if (!pbk_rpg_bb.loadGFX("data/rpg_bb.gfx", mem_flag)) return false;

    debug << "Successfully loaded <rpg_bb.gfx>\n";

    // Fonte score blip
    //
    if (!fnt_score_blip.load("data/scorei.lft", mem_flag)) return false;

    debug << "Successfully loaded <scorei.lft>\n";

    // Fonte score blop
    //
    if (!fnt_score_blop.load("data/scoreo.lft", mem_flag)) return false;

    debug << "Successfully loaded <scoreo.lft>\n";

    // Fonte munitions
    //
    if (!fnt_ammo.load("data/ammo1.lft", mem_flag)) return false;

    debug << "Successfully loaded <ammo1.lft>\n";

    // Fonte munitions utilisées
    //
    if (!fnt_ammo_used.load("data/ammo2.lft", mem_flag)) return false;

    debug << "Successfully loaded <ammo2.lft>\n";

    // Sons globaux
    //
    if (!sbk_misc.loadSFX("data/misc.sfx")) return false;

    debug << "Successfully loaded <misc.sfx>\n";

    // Sons BB
    //
    if (!sbk_bb.loadSFX("data/bb.sfx")) return false;

    debug << "Successfully loaded <bb.sfx>\n";

    // La liste des niveaux
    //
    if (!loadList("data/bb.lst")) return false;

    debug << "Successfully loaded <bb.lst>\n";
    debug << "Successfully loaded all needed global game files\n";

    return true;
}

//-----------------------------------------------------------------------------

template <class T>
void Game::DrawCollection(const T& xs) {
    for (auto& pl : xs) {
        pl->affiche();
    }
}

//-----------------------------------------------------------------------------

template <class T>
void Game::RemoveDestroyed(T& xs) {
    xs.erase(std::remove_if(
                 xs.begin(), xs.end(), [](auto& x) { return x->aDetruire(); }),
             xs.end());
}

void Game::cleanLists() {
    RemoveDestroyed(list_joueurs);
    RemoveDestroyed(list_fonds_statiques);
    RemoveDestroyed(list_tirs_bb);
    RemoveDestroyed(list_cow);
    RemoveDestroyed(list_bulles);
    RemoveDestroyed(list_impacts);
    RemoveDestroyed(list_ennemis);
    RemoveDestroyed(list_bonus);
    RemoveDestroyed(list_gen_ennemis);
    RemoveDestroyed(list_gen_bonus);
    RemoveDestroyed(list_txt_cool);
    RemoveDestroyed(list_fonds_animes);
    RemoveDestroyed(list_premiers_plans);
    RemoveDestroyed(list_giclures);
    RemoveDestroyed(list_tirs_ennemis);
    RemoveDestroyed(list_meteo);
    RemoveDestroyed(list_gore);
    RemoveDestroyed(list_plateformes_mobiles);
}

//-----------------------------------------------------------------------------

void Game::updateEvents() {
    Event* event;

    // Les évenements de la liste "endormie" sont mis dans la liste "en attente"
    // quand ils sont sur le point d'être activés
    //
    // FIXME: These reverse iterators are WEIRD. Why are the events coming
    // in reverse order? This feels so backward.
    for (auto it = list_event_endormis.rbegin();
         it != list_event_endormis.rend();
         ++it) {
        auto& event = *it;
        if (!event->aReveiller()) {
            break;
        }
        list_event.push_back(std::move(event));
    }

    list_event_endormis.erase(
        std::remove_if(list_event_endormis.begin(),
                       list_event_endormis.end(),
                       [](auto& ev) { return !ev.get(); }),
        list_event_endormis.end());

    // Si les évenements "en attente" doivent être activés, on les active
    //
    for (auto& event : list_event) {
        if (event->aActiver()) {
            event->doEvent();
            event.reset(nullptr);
        }
    }
    list_event.erase(std::remove_if(list_event.begin(),
                                    list_event.end(),
                                    [](auto& ev) { return !ev.get(); }),
                     list_event.end());
}

//-----------------------------------------------------------------------------

void Game::manageCollisions() {
    Tir* tir;
    Ennemi* ennemi;

    // Collisions TirsBB / Ennemis
    //
    for (Tir* tir : list_tirs_bb) {
        for (auto& ennemi : list_ennemis) {
            if (tir->collision(ennemi.get())) {
                ennemi->estTouche(tir);
            }
        }
    }

    // Collisions Vaches / Ennemis
    //
    for (auto& tir : list_cow) {
        for (auto& ennemi : list_ennemis) {
            if (tir->collision(ennemi.get())) {
                ennemi->estTouche(tir.get());
            }
        }
    }

    // Collisions Joueurs / Bonus
    //
    for (auto& bonus : list_bonus) {
        for (Couille* couille : list_joueurs) {
            if (bonus->collision(couille)) {
                bonus->estPris(couille);
            }
        }
    }

    if (wait_for_victory <= 0) {
        // Collisions Joueurs / Ennemis
        //
        for (auto& ennemi : list_ennemis) {
            for (Couille* joueur : list_joueurs) {
                if (ennemi->collision(joueur))
                    joueur->estTouche(ennemi->degats());
            }
        }

        // Collisions Joueurs / tirs ennemis
        //
        for (auto& tir : list_tirs_ennemis) {
            for (Couille* joueur : list_joueurs) {
                if (tir->collision(joueur)) {
                    joueur->estTouche(tir->degats());
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------

void Game::updateLock() {
    if (!scroll_locked) return;

    if ((cond_end_lock == 0 && list_ennemis.empty()) ||
        (cond_end_lock == 1 && list_gen_ennemis.empty()) ||
        (cond_end_lock == 2 && game_flag[flag_end_lock] == val_end_lock) ||
        (cond_end_lock == 3 && game_flag[flag_end_lock] >= val_end_lock)) {
        scroll_locked = false;
        go_.Come();
    }
}

//-----------------------------------------------------------------------------

void Game::updateHoldFire() {
    if (!hold_fire) return;

    if (game_flag[flag_hold_fire] == val_hold_fire) hold_fire = false;
}

//-----------------------------------------------------------------------------

void Game::drawHUB() {
    if (player1 != NULL && player1->nb_life > 0) {
        auto color = player1->id_couille == ID_BLIP ? HUD::Color::Blue
                                                    : HUD::Color::Orange;
        hud_.Draw(player1, HUD::Location::Left, color, niveau_bonus);
    }

    if (player2 != NULL && player2->nb_life > 0) {
        auto color = player2->id_couille == ID_BLIP ? HUD::Color::Blue
                                                    : HUD::Color::Orange;
        hud_.Draw(player2, HUD::Location::Right, color, niveau_bonus);
    }
}

//-----------------------------------------------------------------------------

void Game::drawHUBpv(int x, int y, int pv) {
    static const int x_pv[] = {3, 0, 13, 44, 31};
    static const int y_pv[] = {0, 21, 48, 21, 0};

    for (int i = 0; i < pv; i++)
        pbk_bb[201 - i]->BlitTo(backSurface, x + x_pv[i], y + y_pv[i]);

    for (int i = pv; i < 5; i++)
        pbk_bb[196 - i]->BlitTo(backSurface, x + x_pv[i], y + y_pv[i]);
}

//-----------------------------------------------------------------------------

void Game::drawDebugInfos() {
    char buffer[40];

    if (show_fps) {
        sprintf(buffer, "FPS = %d", fps_count);
        fnt_rpg.print(backSurface, 10, 130, buffer);
    }
    /*
            if ( show_lists)
            {
                    sprintf( buffer, "Joueurs = %d", list_joueurs.taille());
                    fnt_rpg.print( backSurface, 10, 150, buffer);

                    sprintf( buffer, "Tirs joueurs = %d",
       list_tirs_bb.taille()); fnt_rpg.print( backSurface, 10, 170, buffer);

                    sprintf( buffer, "Ennemis = %d", list_ennemis.taille());
                    fnt_rpg.print( backSurface, 10, 190, buffer);

                    sprintf( buffer, "Tirs ennemis = %d",
       list_tirs_ennemis.taille()); fnt_rpg.print( backSurface, 10, 210,
       buffer);

                    sprintf( buffer, "Gens ennemis = %d",
       list_gen_ennemis.taille()); fnt_rpg.print( backSurface, 10, 230, buffer);

                    sprintf( buffer, "Nb created = %d", nb_ennemis_created);
                    fnt_rpg.print( backSurface, 10, 250, buffer);

                    sprintf( buffer, "Fonds animes = %d",
       list_fonds_animes.taille()); fnt_rpg.print( backSurface, 10, 270,
       buffer);

                    sprintf( buffer, "Meteo = %d / %d (%d)",
       list_meteo.taille(), intensite_meteo, type_meteo); fnt_rpg.print(
       backSurface, 10, 290, buffer);

                    sprintf( buffer, "Plat. mobile = %d",
       list_plateformes_mobiles.taille()); fnt_rpg.print( backSurface, 10, 310,
       buffer);

                    sprintf( buffer, "Giclures = %d", list_giclures.taille());
                    fnt_rpg.print( backSurface, 10, 330, buffer);

                    sprintf( buffer, "xTex = %d", xTex);
                    fnt_rpg.print( backSurface, 10, 350, buffer);

                    sprintf( buffer, "next_x = %d", next_x);
                    fnt_rpg.print( backSurface, 10, 370, buffer);

                    sprintf( buffer, "Offset = %d", offset);
                    fnt_rpg.print( backSurface, 10, 390, buffer);

                    sprintf( buffer, "n_img = %d", n_img);
                    fnt_rpg.print( backSurface, 10, 410, buffer);

                    sprintf( buffer, "n_cache = %d", n_cache);
                    fnt_rpg.print( backSurface, 10, 430, buffer);

                    sprintf( buffer, "glorf = %d", glorf);
                    fnt_rpg.print( backSurface, 10, 450, buffer);


                    sprintf( buffer, "0-USER0 = %d", game_flag[FLAG_USER0]);
                    fnt_rpg.print( backSurface, 440, 165, buffer);

                    sprintf( buffer, "1-USER1 = %d", game_flag[FLAG_USER1]);
                    fnt_rpg.print( backSurface, 440, 185, buffer);

                    sprintf( buffer, "2-USER2 = %d", game_flag[FLAG_USER2]);
                    fnt_rpg.print( backSurface, 440, 205, buffer);

                    sprintf( buffer, "3-USER3 = %d", game_flag[FLAG_USER3]);
                    fnt_rpg.print( backSurface, 440, 225, buffer);

                    sprintf( buffer, "4-RESERVED = %d", game_flag[3]);
                    fnt_rpg.print( backSurface, 440, 245, buffer);

                    sprintf( buffer, "5-BONUS = %d", game_flag[FLAG_BONUS]);
                    fnt_rpg.print( backSurface, 440, 265, buffer);

                    sprintf( buffer, "6-TIMER = %d", game_flag[FLAG_TIMER]);
                    fnt_rpg.print( backSurface, 440, 285, buffer);

                    sprintf( buffer, "7-GEN_OFF = %d", game_flag[FLAG_GEN_OFF]);
                    fnt_rpg.print( backSurface, 440, 305, buffer);

                    sprintf( buffer, "8-NB_KILL = %d", game_flag[FLAG_NB_KILL]);
                    fnt_rpg.print( backSurface, 440, 325, buffer);

                    sprintf( buffer, "9-NB_ENN = %d", game_flag[FLAG_NB_ENN]);
                    fnt_rpg.print( backSurface, 440, 345, buffer);

                    sprintf( buffer, "10-NB_GEN = %d", game_flag[FLAG_NB_GEN]);
                    fnt_rpg.print( backSurface, 440, 365, buffer);

                    sprintf( buffer, "Channels = %d",
       FSOUND_GetChannelsPlaying()); fnt_rpg.print( backSurface, 440, 405,
       buffer);

                    sprintf( buffer, "Gore = %d", list_gore.taille());
                    fnt_rpg.print( backSurface, 440, 425, buffer);

                    sprintf( buffer, "Fonds stat. = %d",
       list_fonds_statiques.taille()); fnt_rpg.print( backSurface, 440, 445,
       buffer);

                    sprintf( buffer, "Impacts = %d", list_impacts.taille());
                    fnt_rpg.print( backSurface, 440, 465, buffer);

                    RECT	r;

                    r.top	= 10;
                    r.bottom= 150;
                    r.left	= 200;
                    r.right = 440;

                    backSurface->Blt( &r, videoA, NULL, DDBLT_WAIT, 0);
            }
    */
}

//-----------------------------------------------------------------------------

void Game::updateTeteTurc() {
    static int ntete_turc = 0;

    if (list_joueurs.empty()) {
        dummyPlayer.x = offset + 320;
        dummyPlayer.y = y_plat[0][offset + 320];
        tete_turc = &dummyPlayer;
        return;
    }

    ntete_turc += 1;
    ntete_turc %= list_joueurs.size();
    tete_turc = list_joueurs[ntete_turc];
}

//-----------------------------------------------------------------------------

void Game::updateRPG() {
    if (rpg_to_play == -1) return;

    bool continued = true;

    rpg.startPlay(rpg_to_play);

    while (continued && !app_killed) {
        manageMsg();
        checkRestore();
        drawAll(false);
        continued = rpg.drawScene(backSurface);
        DDFlipV();
    }

    rpg.stopPlay();
    in.waitClean();
    rpg_to_play = -1;
    update_regulator_.Skip();
}

//-----------------------------------------------------------------------------

void Game::updateVictoryAndDefeat() {
    // Defaite ?
    //
    if (list_joueurs.empty()) {
        wait_for_death += 1;

        if (wait_for_death >= 200) joueurs_morts = true;

        return;
    }

    // Victoire ?
    //
    if (offset >= vic_x && game_flag[vic_flag1] == vic_val1 &&
        game_flag[vic_flag2] == vic_val2) {
        hold_fire = true;
        wait_for_victory += 1;

        if (game_flag[1] == 999) wait_for_victory = 200;

        if (wait_for_victory >= 200) niveau_fini = true;
    } else {
        if (wait_for_victory > 0) wait_for_victory = 0;
    }
}

//-----------------------------------------------------------------------------

void Game::updateFlags() {
    game_flag[FLAG_NB_GEN] = list_gen_ennemis.size();
    game_flag[FLAG_NB_ENN] = list_ennemis.size();
    makeb_current_mode = game_flag[FLAG_BONUS];

    // Le TIMER
    //
    if (game_flag[FLAG_TIMER] > 0 && wait_for_victory == -1) {
        etape_timer += 1;
        etape_timer %= 80;

        if (etape_timer == 0) {
            game_flag[FLAG_TIMER] -= 1;

            if (game_flag[FLAG_TIMER] == 0 && niveau_bonus) niveau_fini = true;
        }
    }
}

//-----------------------------------------------------------------------------

void Game::drawTimer() {
    if (game_flag[FLAG_TIMER] > 0) {
        char buffer[10];

        fnt_cool.printC(backSurface, 320, 20, txt_data[TXT_TIME].c_str());
        sprintf(buffer, "%d", game_flag[FLAG_TIMER]);
        fnt_cool.printC(backSurface, 320, 50, buffer);
    }
}

//-----------------------------------------------------------------------------

void Game::updateMenu() {
    // Appelle le menu s'il y a lieu de le faire
    //
    if (in.scanKey(DIK_ESCAPE)) {
        MenuGame menu;
        int r;

        drawAll(false);
        DDFlipV();
        drawAll(false);
        DDFlipV();

        menu.Draw(backSurface);
        DDFlipV();
        // primSurface->Flip(NULL, DDFLIP_WAIT );

        in.waitClean();

        while (!(r = menu.Update()) && !app_killed) {
            manageMsg();
            checkRestore();
            drawAll(false);
            menu.Draw(backSurface);

            DDFlipV();
            // primSurface->Flip(NULL, DDFLIP_WAIT );
        }

        if (r == 2) skipped = true;

        drawAll(false);
        DDFlipV();
        drawAll(false);
        DDFlipV();

        update_regulator_.Skip();
    }
}

//-----------------------------------------------------------------------------

void Game::updateCheat() {
    if (wait_cheat < 20) wait_cheat += 1;

    if (!cheat_on) return;

    if (player1 != NULL) {
        if (in.scanKey(DIK_F1)) player1->ammo += 10;

        if (wait_cheat < 20) return;

        if (in.scanKey(DIK_F3)) {
            player1->nb_life++;
            wait_cheat = 0;
        }

        if (in.scanKey(DIK_F2)) {
            player1->nb_cow_bomb += 1;
            wait_cheat = 0;
        }
    }

    if (player2 != NULL) {
        if (in.scanKey(DIK_F5)) player2->ammo += 10;

        if (wait_cheat < 20) return;

        if (in.scanKey(DIK_F7)) {
            player2->nb_life++;
            wait_cheat = 0;
        }

        if (in.scanKey(DIK_F6)) {
            player2->nb_cow_bomb += 1;
            wait_cheat = 0;
        }
    }

    if (in.scanKey(DIK_F9)) niveau_fini = true;

    if (wait_cheat < 20) return;

    if (in.scanKey(DIK_F12)) {
        show_fps = !show_fps;
        wait_cheat = 0;
    }

    if (in.scanKey(DIK_F11)) {
        show_lists = !show_lists;
        wait_cheat = 0;
    }
}

//-----------------------------------------------------------------------------

void Game::showPE(bool bonus, bool fuckOff) {
    Fonte* fnt_p1;
    Fonte* fnt_p2;
    char buffer[20];
    int killed_p1 = 0;    // % d'ennemis tués par le joueur 1
    int c_killed_p1 = 0;  // compteur killed_p1
    int calc_p1 = 0;
    int c_time_p1 = 0;
    int total_bonus_p1 = 0;
    int total_p1 = 0;
    int obj_p1 = 0;
    int c_obj_p1 = 0;
    int perfect_p1 = 0;
    int c_perfect_p1 = 0;
    int life_up_p1 = 0;

    int killed_p2 = 0;    // % d'ennemis tués par le joueur 1
    int c_killed_p2 = 0;  // compteur killed_p2
    int calc_p2 = 0;
    int c_time_p2 = 0;
    int total_bonus_p2 = 0;
    int total_p2 = 0;
    int obj_p2 = 0;
    int c_obj_p2 = 0;
    int perfect_p2 = 0;
    int c_perfect_p2 = 0;
    int life_up_p2 = 0;

    int delai = 0;
    int xbasep1;
    bool showp1;
    int xbasep2;
    bool showp2;

    systemSurface->BltFast(
        0, 0, backSurface, NULL, DDBLTFAST_NOCOLORKEY | DDBLTFAST_WAIT);

    Rect r;

    r.top = 100;
    r.left = 60;
    r.right = 580;
    r.bottom = 400;

    LGXpaker.halfTone(systemSurface, &r);

    mbk_inter.play(0);

    if (list_joueurs.size() == 2) {
        showp1 = showp2 = true;
        xbasep1 = 80;
        xbasep2 = 350;
    } else {
        if (player1 != NULL && player1->nb_life > 0) {
            showp1 = true;
            showp2 = false;
            xbasep1 = 210;
            calc_p2 = 4;
        } else if (player2 != NULL) {
            showp1 = false;
            showp2 = true;
            xbasep2 = 210;
            calc_p1 = 4;
        }
    }

    if (showp1) {
        if (player1->id_couille == ID_BLIP)
            fnt_p1 = &fnt_score_blip;
        else
            fnt_p1 = &fnt_score_blop;
    }

    if (showp2) {
        if (player2->id_couille == ID_BLIP)
            fnt_p2 = &fnt_score_blip;
        else
            fnt_p2 = &fnt_score_blop;
    }

    if (nb_ennemis_created > 0) {
        if (showp1)
            killed_p1 = (100 * player1->getKilled()) / nb_ennemis_created;
        if (showp2)
            killed_p2 = (100 * player2->getKilled()) / nb_ennemis_created;
    }

    if (!bonus || (game_flag[FLAG_TIMER] > 0 && !joueurs_morts)) {
        if (showp1) obj_p1 = 50000;
        if (showp2) obj_p2 = 50000;
    }

    if (obj_p1 > 0 && player1->perfect) perfect_p1 = 50000;

    if (obj_p2 > 0 && player2->perfect) perfect_p2 = 50000;

    if (showp1) total_p1 = player1->getScore();

    if (showp2) total_p2 = player2->getScore();

    if (fuckOff) {
        perfect_p1 = perfect_p2 = obj_p1 = obj_p2 = killed_p1 = killed_p2 = 0;
        game_flag[FLAG_TIMER] = 0;
    }

    while (!app_killed && (calc_p1 < 4 || calc_p2 < 4)) {
        manageMsg();
        checkRestore();

        if (app_killed) return;

        in.update();

        delai += 1;
        delai %= 3;

        if (in.anyKeyPressed()) delai = 0;

        if (delai == 0) {
            // Calculs P1
            //
            if (calc_p1 == 0) {
                // Perfect
                //
                if (c_perfect_p1 >= perfect_p1)
                    calc_p1 = 1;
                else {
                    c_perfect_p1 += 1000;
                    total_bonus_p1 += 1000;
                    total_p1 += 1000;
                }
            } else if (calc_p1 == 1) {
                // Bonus objectif atteint
                //
                if (c_obj_p1 >= obj_p1)
                    calc_p1 = 2;
                else {
                    c_obj_p1 += 1000;
                    total_bonus_p1 += 1000;
                    total_p1 += 1000;
                }
            }
            if (calc_p1 == 2) {
                // Bonus % d'ennemis tués
                //
                if (c_killed_p1 >= killed_p1)
                    calc_p1 = 3;  // Passe en TIMER
                else {
                    c_killed_p1 += 1;
                    total_bonus_p1 += 500;
                    total_p1 += 500;
                }
            } else if (calc_p1 == 3) {
                // Bonus temps restant
                //
                if (c_time_p1 >= game_flag[FLAG_TIMER])
                    calc_p1 = 4;  // Fini le calcul
                else {
                    c_time_p1 += 1;
                    total_bonus_p1 += 1000;
                    total_p1 += 1000;
                }
            }

            // Calculs p2
            //
            if (calc_p2 == 0) {
                // Perfect
                //
                if (c_perfect_p2 >= perfect_p2)
                    calc_p2 = 1;
                else {
                    c_perfect_p2 += 1000;
                    total_bonus_p2 += 1000;
                    total_p2 += 1000;
                }
            } else if (calc_p2 == 1) {
                // Bonus objectif atteint
                //
                if (c_obj_p2 >= obj_p2)
                    calc_p2 = 2;
                else {
                    c_obj_p2 += 1000;
                    total_bonus_p2 += 1000;
                    total_p2 += 1000;
                }
            }
            if (calc_p2 == 2) {
                // Bonus % d'ennemis tués
                //
                if (c_killed_p2 >= killed_p2)
                    calc_p2 = 3;  // Passe en TIMER
                else {
                    c_killed_p2 += 1;
                    total_bonus_p2 += 500;
                    total_p2 += 500;
                }
            } else if (calc_p2 == 3) {
                // Bonus temps restant
                //
                if (c_time_p2 >= game_flag[FLAG_TIMER])
                    calc_p2 = 4;  // Fini le calcul
                else {
                    c_time_p2 += 1;
                    total_bonus_p2 += 1000;
                    total_p2 += 1000;
                }
            }
        }

        // Affichage
        //
        backSurface->BltFast(
            0, 0, systemSurface, NULL, DDBLTFAST_NOCOLORKEY | DDBLTFAST_WAIT);

        if (fuckOff) {
            fnt_cool.printC(backSurface, 320, 120, "BONUS STAGE FAILED!");
        } else {
            fnt_cool.printC(backSurface, 320, 120, "LEVEL COMPLETE!");
        }

        // Affichage	P1
        //

        if (showp1) {
            if (life_up_p1 > 0) {
                fnt_rpg.printC(backSurface, xbasep1 + 105, 270, "LIFE UP");
                //				life_up_p1--;
            }

            if (perfect_p1 > 0) {
                strcpy(buffer, "PERFECT");
                fnt_rpg.print(backSurface, xbasep1, 180, buffer);
                sprintf(buffer, "%d", c_perfect_p1);
                fnt_p1->printR(backSurface, xbasep1 + 210, 180, buffer);
            }

            if (obj_p1 > 0) {
                strcpy(buffer, "Objective");
                fnt_rpg.print(backSurface, xbasep1, 200, buffer);
                sprintf(buffer, "%d", c_obj_p1);
                fnt_p1->printR(backSurface, xbasep1 + 210, 200, buffer);
            }

            strcpy(buffer, "Casualties");
            fnt_rpg.print(backSurface, xbasep1, 220, buffer);
            sprintf(buffer, "%d", c_killed_p1);
            fnt_p1->printR(backSurface, xbasep1 + 210, 220, buffer);
            strcpy(buffer, "%");
            fnt_rpg.printR(backSurface, xbasep1 + 220, 220, buffer);

            if (game_flag[FLAG_TIMER] > 0) {
                strcpy(buffer, "Time left");
                fnt_rpg.print(backSurface, xbasep1, 240, buffer);
                sprintf(buffer, "%d", c_time_p1);
                fnt_p1->printR(backSurface, xbasep1 + 210, 240, buffer);
            }

            strcpy(buffer, "BONUS");
            fnt_rpg.print(backSurface, xbasep1, 300, buffer);
            sprintf(buffer, "%d", total_bonus_p1);
            fnt_p1->printR(backSurface, xbasep1 + 210, 300, buffer);

            strcpy(buffer, "SCORE");
            fnt_rpg.print(backSurface, xbasep1, 320, buffer);
            sprintf(buffer, "%d", total_p1);
            fnt_p1->printR(backSurface, xbasep1 + 210, 320, buffer);

            if (total_p1 / 500000 > player1->mod_life) {
                player1->mod_life = total_p1 / 500000;
                player1->nb_life++;
                life_up_p1 = 100;
            }
        }

        // Affichage	P2
        //
        if (showp2) {
            if (life_up_p2 > 0) {
                fnt_rpg.printC(backSurface, xbasep2 + 105, 270, "LIFE UP");
                //				life_up_p2--;
            }

            if (perfect_p2 > 0) {
                strcpy(buffer, "PERFECT");
                fnt_rpg.print(backSurface, xbasep2, 180, buffer);
                sprintf(buffer, "%d", c_perfect_p2);
                fnt_p2->printR(backSurface, xbasep2 + 210, 180, buffer);
            }

            if (obj_p2 > 0) {
                strcpy(buffer, "Objective");
                fnt_rpg.print(backSurface, xbasep2, 200, buffer);
                sprintf(buffer, "%d", c_obj_p2);
                fnt_p2->printR(backSurface, xbasep2 + 210, 200, buffer);
            }

            strcpy(buffer, "Casualties");
            fnt_rpg.print(backSurface, xbasep2, 220, buffer);
            sprintf(buffer, "%d", c_killed_p2);
            fnt_p2->printR(backSurface, xbasep2 + 210, 220, buffer);
            strcpy(buffer, "%");
            fnt_rpg.printR(backSurface, xbasep2 + 220, 220, buffer);

            if (game_flag[FLAG_TIMER] > 0) {
                strcpy(buffer, "Time left");
                fnt_rpg.print(backSurface, xbasep2, 240, buffer);
                sprintf(buffer, "%d", c_time_p2);
                fnt_p2->printR(backSurface, xbasep2 + 210, 240, buffer);
            }

            strcpy(buffer, "BONUS");
            fnt_rpg.print(backSurface, xbasep2, 300, buffer);
            sprintf(buffer, "%d", total_bonus_p2);
            fnt_p2->printR(backSurface, xbasep2 + 210, 300, buffer);

            strcpy(buffer, "SCORE");
            fnt_rpg.print(backSurface, xbasep2, 320, buffer);
            sprintf(buffer, "%d", total_p2);
            fnt_p2->printR(backSurface, xbasep2 + 210, 320, buffer);

            if (total_p2 / 500000 > player2->mod_life) {
                player2->mod_life = total_p2 / 500000;
                player2->nb_life++;
                life_up_p2 = 100;
            }
        }

        DDFlipV();
    }

    if (!app_killed) {
        in.waitClean();
        in.waitKey();
    }

    if (showp1) player1->setScore(total_p1);
    if (showp2) player2->setScore(total_p2);

    mbk_inter.stop();
}

//-----------------------------------------------------------------------------

void Game::updateMeteo() {
    const static int speed_gouttes[] = {3, 5, 7, 8, 9, 10};

    for (auto& pl : list_meteo) {
        pl->update();
    }

    while (list_meteo.size() < intensite_meteo) {
        if (type_meteo == METEO_NEIGE) {
            //			MeteoNeige * flocon = new MeteoNeige();

            next_flocon = (next_flocon + 1) % NB_FLOCONS;
            MeteoNeige* flocon = &neige[next_flocon];

            int d = rand() % 3;

            flocon->a_detruire = false;
            flocon->xbase = offset + rand() % 800;
            flocon->y = rand() % 480 - 500;

            if (d <= 1)
                flocon->pic = pbk_misc[71];
            else
                flocon->pic = pbk_misc[72];

            flocon->dy = intensite_meteo / 20 + d;
            flocon->phi = rand() % 360;
            flocon->xwide = 10 + d * 4;

            if (mur_opaque(flocon->xbase, 0)) flocon->y -= 550;

            list_meteo.emplace_back(flocon);
        } else if (type_meteo == METEO_PLUIE) {
            //			MeteoPluie * goutte = new MeteoPluie();

            next_goutte = (next_goutte + 1) % NB_GOUTTES;
            MeteoPluie* goutte = &pluie[next_goutte];

            int d = rand() % 4;

            goutte->a_detruire = false;
            goutte->x = offset + rand() % 800;
            goutte->y = rand() % 480 - 480;
            goutte->pic = pbk_misc[65 + d];
            goutte->dy = 7 + d * 2;

            if (mur_opaque(goutte->x, 0)) goutte->y -= 550;

            list_meteo.emplace_back(goutte);
        }
    }
}

//-----------------------------------------------------------------------------

template <class T>
void Game::UpdateCollection(const T& xs) {
    for (auto& pl : xs) {
        pl->update();
    }
}

//-----------------------------------------------------------------------------

void Game::updateFlecheGo() {
    go_.Update();
    if (last_x_go_ != offset) {
        last_x_go_ = offset;
        go_.Leave();
    }
}

//-----------------------------------------------------------------------------

void Game::updateDeformation() {
    phi_deform += 2;
    phi_deform %= 360;
}

//-----------------------------------------------------------------------------

void Game::drawDeformation() {
    DDBLTFX ddfx;
    Rect r;
    int pas = 20;
    int phi = phi_deform;
    int dphi = 10;
    int x;
    int xt;

    memset(&ddfx, 0, sizeof(ddfx));
    ddfx.dwSize = sizeof(ddfx);
    ddfx.dwFillColor = 0;  // Noir

    pas = 2;
    dphi = 1;

    for (int y = 0; y < 480; y += pas) {
        phi += dphi;
        phi %= 360;

        r.top = y;
        r.bottom = y + pas;

        x = xt = sini(5, phi);

        // FIXME: This is absolutely wrong. We are copying a memory location to
        // another that is overlapping (moving a line a few pixels). Depending
        // on the order of overlap, we might be (and are) overwriting the data
        // we're copying from and screwing everything up.  A temp satisfying
        // fix would involve allocating a third surface to safely copy or doing
        // it ourselves by locking the surface into host memory. A long term
        // and much better fix is using pixel shaders.

        if (x < 0) {
            r.left = -x;
            r.right = 640;
            x = 0;
        } /*else {
            // I'm overwriting my own mem!
            r.left = 0;
            r.right = 640 - x;
        }*/

        backSurface->BltFast(
            x, y, backSurface, &r, DDBLTFAST_WAIT | DDBLTFAST_NOCOLORKEY);

        if (xt < 0) {
            r.left = 640 + xt;
            r.right = 640;
        } else {
            r.left = 0;
            r.right = xt;
        }

        backSurface->Blt(&r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddfx);
    }
}

//-----------------------------------------------------------------------------

void Game::updateBulles() {
    Sprite* pl;

    // Crée de nouvelles bulles
    //
    for (Couille* pl : list_joueurs) {
        creeBulle(pl);
    }

    for (auto& pl : list_ennemis) {
        if (list_bulles.size() < 15) {
            break;
        }
        creeBulle(pl.get());
    }

    // Update les bulles déjà crées
    //
    for (auto& pl : list_bulles) {
        pl->update();
    }
}

//-----------------------------------------------------------------------------

void Game::creeBulle(Sprite* s) {
    s->wait_bulle += rand() % 10;

    if (s->wait_bulle > 300) {
        Bulle* b = new Bulle();

        b->xbase = s->x;
        b->y = s->y - 10;
        b->pic = pbk_misc[78 + rand() % 3];
        b->dphi = 5 + rand() % 3;
        b->dy = -(2 + rand() % 2);

        list_bulles.emplace_back(b);

        s->wait_bulle = 0;
    }
}

//-----------------------------------------------------------------------------

void Game::updateTremblements() {
    if (amplitude_tremblement == 0) return;

    if (etape_tremblement == 0) {
        dy_tremblement += ddy_tremblement;

        if (dy_tremblement <= -amplitude_tremblement) {
            amplitude_tremblement -= 1;
            ddy_tremblement = (amplitude_tremblement >> 1) + 1;
            dy_tremblement += 1;
        } else if (dy_tremblement >= amplitude_tremblement) {
            amplitude_tremblement -= 1;
            ddy_tremblement = -(amplitude_tremblement >> 1) - 1;
            dy_tremblement -= 1;
        }
    }
}

//-----------------------------------------------------------------------------

void Game::drawTremblements() {
    if (amplitude_tremblement == 0 || dy_tremblement == 0) return;

    int y;
    Rect r;
    Rect r2;

    r2.left = r.left = 0;
    r2.right = r.right = 640;

    if (dy_tremblement < 0) {
        r.top = -dy_tremblement;
        r.bottom = 480;

        r2.top = 480 + dy_tremblement;
        r2.bottom = 480;

        y = 0;
    } else if (dy_tremblement > 0) {
        r.top = 0;
        r.bottom = 480 - dy_tremblement;

        r2.top = 0;
        r2.bottom = dy_tremblement;

        y = dy_tremblement;
    }

    backSurface->BltFast(
        0, y, backSurface, &r, DDBLTFAST_WAIT | DDBLTFAST_NOCOLORKEY);

    DDBLTFX ddfx;

    memset(&ddfx, 0, sizeof(ddfx));
    ddfx.dwSize = sizeof(ddfx);
    ddfx.dwFillColor = 0;

    backSurface->Blt(&r2, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddfx);
}

//-----------------------------------------------------------------------------

bool Game::loadList(const char* fic) {
    std::ifstream f;
    int n;

    f.open(fic);

    if (f.is_open() == 0) {
        debug << "Cannot open " << fic << "\n";
        return false;
    }

    nb_part = 0;
    f >> n;

    while (!f.eof()) {
        if (n < 0 || n > 2) {
            debug << "File " << fic << " corrupted!\n";
            f.close();
            return false;
        }

        type_part[nb_part] = n;

        if (n == PART_CINE || n == PART_BRIEFING) {
            f.getline(fic_names[nb_part], 200, '*');
            f.getline(fic_names[nb_part], 200);
        } else {
            f >> type_lvl[nb_part];
            f.getline(fic_names[nb_part], 200, '*');
            f.getline(fic_names[nb_part], 200);
        }

        nb_part += 1;
        f >> n;

        if (nb_part > MAX_PART) {
            debug << "File " << fic << " corrupted!\n";
            f.close();
            return false;
        }
    }

    f.close();
    return true;
}

//-----------------------------------------------------------------------------

void Game::drawLoading() {
    Rect r;

    r.left = 0;
    r.right = 640;
    r.top = 195;
    r.bottom = 245;

    //	LGXpaker.halfTone( backSurface, &r);

    //	fnt_menu.printC( backSurface, 320, 205, "LOADING");
    fnt_cool.printC(backSurface, 320, 205, "LOADING");
}

//-----------------------------------------------------------------------------

void Game::getHiscore() {
    if (player1 != NULL && hi_scores.isGood(player1->getScore()))
        getName(player1, 1);

    if (player2 != NULL && hi_scores.isGood(player2->getScore()))
        getName(player2, 2);
}

//-----------------------------------------------------------------------------

void Game::getName(Joueur* joueur, int ijoueur) {
    char buff[30];
    char name[21];
    int i = 0;
    int key = -1;
    int x = 640;

    sprintf(buff, "PLAYER %d GOT A HIGH SCORE!", ijoueur);

    name[0] = '\0';

    strcpy(name, "");

    while (!app_killed && x >= 40) {
        manageMsg();
        checkRestore();

        x -= 20;

        pbk_inter[1]->PasteTo(backSurface, 0, 0);

        fnt_menu.printC(backSurface, 320 - x, 160, buff);
        fnt_menu.printC(backSurface, 320 + x, 210, "PLEASE ENTER YOUR NAME :");

        DDFlipV();
    }

    while (!app_killed && key != DIK_RETURN) {
        manageMsg();
        checkRestore();

        in.waitClean();
        key = in.waitKey();

        std::string key_tmp = DIK_to_string(key);

        if (key == DIK_SPACE && i < 19) {
            name[i++] = ' ';
            name[i] = '\0';
        }
        if (key == DIK_BACKSPACE && i > 0) {
            name[--i] = '\0';
        } else if (key_tmp.size() == 1 && i < 19) {
            name[i++] = key_tmp[0];
            name[i] = '\0';
        }

        pbk_inter[1]->PasteTo(backSurface, 0, 0);
        fnt_menu.printC(backSurface, 320 - x, 160, buff);
        fnt_menu.printC(backSurface, 320 + x, 210, "PLEASE ENTER YOUR NAME :");
        fnt_menu.printC(backSurface, 320, 260, name);
        // primSurface->Flip( NULL, 0);
        DDFlipV();
    }

    while (!app_killed && x < 640) {
        x += 20;

        pbk_inter[1]->PasteTo(backSurface, 0, 0);

        fnt_menu.printC(backSurface, 320 - x, 160, buff);
        fnt_menu.printC(backSurface, 320 + x, 210, "PLEASE ENTER YOUR NAME :");
        fnt_menu.printC(backSurface, 320, 260 + x, name);

        DDFlipV();  // primSurface->Flip( NULL, 0);
    }

    hi_scores.add(joueur->getScore(), name);
}

//-----------------------------------------------------------------------------

void Game::showGameOver() {
    int x = 400;

    mbk_inter.play(1);

    while (!app_killed && x > 20 && !in.anyKeyPressed()) {
        manageMsg();
        checkRestore();

        pbk_inter[0]->PasteTo(backSurface, 0, 0);
        /*
                        fnt_menu.printR( backSurface, 320-x, 220, "GAME");
                        fnt_menu.print( backSurface, 320+x, 220, "OVER");

                        x -= 1;
        */
        DDFlipV();  // primSurface->Flip( NULL, 0);
    }
    /*
            if ( !app_killed)
            {
                    in.waitClean();
                    in.waitKey();
            }

            while ( !app_killed && x < 400)
            {
                    manageMsg();
                    checkRestore();

                    pbk_inter[0]->PasteTo( backSurface, 0, 0);

                    fnt_menu.printR( backSurface, 320-x, 220, "GAME");
                    fnt_menu.print( backSurface, 320+x, 220, "OVER");

                    x += 20;

                    DDFlipV();
            }
    */
    mbk_inter.stop();
}

void Game::showHighScores() {
    char buffer[50];
    int x[HS_NB_SCORES];

    for (int i = 0; i < HS_NB_SCORES; i++) x[i] = 400 + 160 * i;

    Chrono scores_timer;
    while (!app_killed && !in.anyKeyPressed() &&
           (scores_timer.elapsed() <= 10000)) {
        manageMsg();
        checkRestore();

        pbk_inter[1]->PasteTo(backSurface, 0, 0);

        for (int i = 0; i < HS_NB_SCORES; i++) {
            if (x[i] > 0) x[i] -= 20;

            sprintf(buffer, "%d", hi_scores.getScore(i));
            fnt_cool.printR(backSurface, 300 - x[i], 160 + 30 * i, buffer);

            sprintf(buffer, "%s", hi_scores.getName(i));
            fnt_cool.print(backSurface, 320 + x[i], 160 + 30 * i, buffer);
        }

        DDFlipV();  // primSurface->Flip( NULL, 0);
    }

    while (!app_killed && x[HS_NB_SCORES - 1] < 350) {
        manageMsg();
        checkRestore();

        pbk_inter[1]->PasteTo(backSurface, 0, 0);

        for (int i = 0; i < HS_NB_SCORES; i++) {
            if (i == 0 || x[i - 1] >= 160) x[i] += 20;

            sprintf(buffer, "%d", hi_scores.getScore(i));
            fnt_cool.printR(backSurface, 300 - x[i], 160 + 30 * i, buffer);

            sprintf(buffer, "%s", hi_scores.getName(i));
            fnt_cool.print(backSurface, 320 + x[i], 160 + 30 * i, buffer);
        }

        DDFlipV();  // primSurface->Flip( NULL, 0);
    }

    in.waitClean();
}

//-----------------------------------------------------------------------------

void Game::go() {
    TitleScreen menu;
    int r;
    static int zob = 0;

    // Clear l'écran en noir + LOADING
    //
    {
        DDBLTFX ddfx;

        memset(&ddfx, 0, sizeof(ddfx));
        ddfx.dwSize = sizeof(ddfx);
        ddfx.dwFillColor = 0;

        backSurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddfx);
        fnt_cool.printC(backSurface, 320, 240, "PLEASE WAIT");
    }

    CINEPlayer cine;

    cine.loadPBK("data/intro.gfx");
    mbk_interl.play(0);
    cine.playScene("data/intro.cin", primSurface, backSurface);

    menu.start();
    in.waitClean();

    Chrono diff_start;

    do {
        r = RET_CONTINUE;

        while (!r && !app_killed) {
            manageMsg();
            checkRestore();

            in.update();

            if (in.anyKeyPressed()) {
                diff_start.Reset();
            }

            if (diff_start.elapsed() >= 10000) {
                switch (zob) {
                    case 0:
                    case 2:
                    case 4:
                    case 6:
                        showMainScreen();
                        break;

                    case 1:
                        showHighScores();
                        break;

                    case 5:
                        showCredits();
                        break;

                    case 3:
                    case 7:
                        cine.loadPBK("data/intro.gfx");
                        cine.playScene(
                            "data/intro.cin", primSurface, backSurface);
                        break;
                }

                zob = (zob + 1) % 8;
                //				diff_start = GetTickCount();

                menu.stop();
                menu.start();

                if (in.anyKeyPressed()) {
                    diff_start.Reset();
                    in.waitClean();
                }
            } else {
                pbk_inter[1]->PasteTo(backSurface, 0, 0);

                r = menu.update();
                menu.draw(backSurface);

                DDFlipV();
            }
        }

        if (r == RET_START_GAME1 || r == RET_START_GAME2) {
            CharacterSelection select;

            auto selected = CharacterSelection::Output::Continue;
            while (selected == CharacterSelection::Output::Continue &&
                   !app_killed) {
                manageMsg();
                checkRestore();
                in.update();
                selected = select.update();
                select.draw();
                DDFlipV();
            }

            drawLoading();
            DDFlipV();

            if (r == RET_START_GAME1)
                jouePartie(1, int(selected));
            else
                jouePartie(2, int(selected));

            menu.stop();
            menu.start();
            mbk_interl.play(0);
            diff_start.Reset();
        }

    } while (r != RET_EXIT && !app_killed);

    menu.stop();

    mbk_interl.stop();
}

//-----------------------------------------------------------------------------

void Game::showBriefing(char* fn) {
    pbk_briefing.loadGFX(fn, DDSURF_SYSTEM, false);

    briefing = true;

    DDBLTFX ddfx;
    Rect r;

    memset(&ddfx, 0, sizeof(ddfx));
    ddfx.dwSize = sizeof(ddfx);
    ddfx.dwFillColor = 0;  // Noir

    r.left = 0;
    r.right = 640;
    r.top = 0;
    r.bottom = 480;

    backSurface->Blt(&r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddfx);

    pbk_briefing[0]->PasteTo(backSurface, 0, 0);
    fnt_rpg.printC(backSurface, 320, 460, "Loading...");
    DDFlip();
}

//-----------------------------------------------------------------------------

void Game::showMainScreen() {
    Chrono t;

    while (!in.anyKeyPressed() && !app_killed && t.elapsed() <= 5000) {
        manageMsg();
        checkRestore();

        in.update();

        pbk_inter[1]->PasteTo(backSurface, 0, 0);
        DDFlipV();  // primSurface->Flip(NULL, DDFLIP_WAIT );
    }
}

//-----------------------------------------------------------------------------

void Game::showCredits(bool theEnd) {
    static const int NB_PAGE = 6;
    static const int ILIGNE = 20;
    static const int ITITRE = 40;
    static const int IPARTI = 120;
    static const int SSPEED = 2;

    int npage = 0;
    Chrono t;
    int y = 480;
    int ey = 0;
    int last_y = 100;
    int xcred = 320;

    PictureBank pbk_cred;

    if (theEnd) {
        xcred = 510;
        pbk_cred.loadGFX("data/credits.gfx", DDSURF_BEST, false);
    }

    update_regulator_.Skip();

    while (!app_killed && (!in.anyKeyPressed() || theEnd) && last_y > 0) {
        int n_updates = update_regulator_.Step();
        for (; n_updates > 0; --n_updates) {
            ey = (ey + 1) % SSPEED;
            if (ey == 0) {
                y--;
            }
        }

        manageMsg();
        checkRestore();
        in.update();

        if (mustFixGforceBug) {
            Rect r;

            r.top = 10;
            r.bottom = 150;
            r.left = 200;
            r.right = 440;

            backSurface->Blt(&r, videoA, NULL, DDBLT_WAIT, NULL);
        }

        {
            DDBLTFX ddfx;
            Rect r;

            memset(&ddfx, 0, sizeof(ddfx));
            ddfx.dwSize = sizeof(ddfx);
            ddfx.dwFillColor = 0;  // Noir

            r.left = 0;
            r.right = 640;
            r.top = 0;
            r.bottom = 480;

            backSurface->Blt(
                &r, NULL, NULL, DDBLT_WAIT | DDBLT_COLORFILL, &ddfx);
        }

        if (theEnd) {
            pbk_cred[npage]->PasteTo(backSurface, 0, 0);
        } else {
            pbk_inter[1]->BlitTo(backSurface, 0, 0);
        }

        fnt_rpg.printC(backSurface, xcred, y + ILIGNE, "Credits");

        int y1 = y + ILIGNE + IPARTI;

        fnt_rpg.printC(backSurface, xcred, y1, "CODE");
        fnt_rpg.printC(
            backSurface, xcred, y1 + ITITRE + ILIGNE * 0, "Benjamin Karaban");
        fnt_rpg.printC(
            backSurface, xcred, y1 + ITITRE + ILIGNE * 1, "Sylvain Bugat");
        fnt_rpg.printC(
            backSurface, xcred, y1 + ITITRE + ILIGNE * 3, "2019 REVAMP");
        fnt_rpg.printC(
            backSurface, xcred, y1 + ITITRE + ILIGNE * 4, "Guillaume Sanchez");

        int y2 = y1 + 2 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y2, "MUSIC AND SOUND");
        fnt_rpg.printC(backSurface, xcred, y2 + ILIGNE, "DESIGN");
        fnt_rpg.printC(
            backSurface, xcred, y2 + ITITRE + ILIGNE, "Gerard Kelly");

        int y3 = y2 + 2 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y3, "ADDITIONAL MUSIC");
        fnt_rpg.printC(backSurface, xcred, y3 + ITITRE + 0 * ILIGNE, "El Mobo");
        fnt_rpg.printC(backSurface, xcred, y3 + ITITRE + 1 * ILIGNE, "Rik Ede");

        int y4 = y3 + 2 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y4, "ARTWORK");
        fnt_rpg.printC(
            backSurface, xcred, y4 + ITITRE + ILIGNE * 0, "Laurent Schneider");
        fnt_rpg.printC(
            backSurface, xcred, y4 + ITITRE + ILIGNE * 1, "Jérémie Comarmond");
        fnt_rpg.printC(
            backSurface, xcred, y4 + ITITRE + ILIGNE * 2, "Jérôme Karaban");
        fnt_rpg.printC(
            backSurface, xcred, y4 + ITITRE + ILIGNE * 3, "Didier Colin");
        fnt_rpg.printC(
            backSurface, xcred, y4 + ITITRE + ILIGNE * 4, "Sylvain Bugat");

        int y5 = y4 + 5 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y5, "ADDITIONAL SOUND");
        fnt_rpg.printC(backSurface, xcred, y5 + ILIGNE, "AND VOCAL EFFECTS");
        fnt_rpg.printC(
            backSurface, xcred, y5 + ITITRE + 1 * ILIGNE, "Eric Khodja");
        fnt_rpg.printC(
            backSurface, xcred, y5 + ITITRE + 2 * ILIGNE, "Jérôme Karaban");
        fnt_rpg.printC(
            backSurface, xcred, y5 + ITITRE + 3 * ILIGNE, "Benjamin Karaban");
        fnt_rpg.printC(
            backSurface, xcred, y5 + ITITRE + 4 * ILIGNE, "Vincent Lagarrigue");

        int y6 = y5 + 5 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y6, "STORYLINE");
        fnt_rpg.printC(
            backSurface, xcred, y6 + ITITRE + 0 * ILIGNE, "Benjamin Karaban");
        fnt_rpg.printC(
            backSurface, xcred, y6 + ITITRE + 1 * ILIGNE, "Eric Khodja");

        int y7 = y6 + 2 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y7, "ORIGINAL CONCEPT");
        fnt_rpg.printC(
            backSurface, xcred, y7 + ITITRE + 0 * ILIGNE, "Laurent Schneider");
        fnt_rpg.printC(
            backSurface, xcred, y7 + ITITRE + 1 * ILIGNE, "Jérémie Comarmond");
        fnt_rpg.printC(
            backSurface, xcred, y7 + ITITRE + 2 * ILIGNE, "Didier Colin");
        fnt_rpg.printC(
            backSurface, xcred, y7 + ITITRE + 3 * ILIGNE, "Sylvain Bugat");
        fnt_rpg.printC(
            backSurface, xcred, y7 + ITITRE + 4 * ILIGNE, "Benjamin Karaban");

        int y8 = y7 + 5 * ILIGNE + IPARTI + ITITRE;

        fnt_rpg.printC(backSurface, xcred, y8, "BETA TEST");
        fnt_rpg.printC(
            backSurface, xcred, y8 + ITITRE + 0 * ILIGNE, "Jérémie Khodja");
        fnt_rpg.printC(
            backSurface, xcred, y8 + ITITRE + 1 * ILIGNE, "Julien Areas");
        fnt_rpg.printC(
            backSurface, xcred, y8 + ITITRE + 2 * ILIGNE, "Carlos Sédille");
        fnt_rpg.printC(
            backSurface, xcred, y8 + ITITRE + 3 * ILIGNE, "Juliette Karaban");

        int y9 = y8 + 4 * ILIGNE + IPARTI + ITITRE;
        int y10 = y9;
        int y11 = y10;

        if (theEnd) {
            fnt_rpg.printC(backSurface, xcred, y9, "THANKS TO");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 0 * ILIGNE, "Arnaud Dubois");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 1 * ILIGNE, "Carole Schmitt");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y9 + ITITRE + 2 * ILIGNE,
                           "Aasterion staff");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 3 * ILIGNE, "Aurore Anger");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y9 + ITITRE + 4 * ILIGNE,
                           "Cécile Baudoncourt");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y9 + ITITRE + 5 * ILIGNE,
                           "Nicolas Clément");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 6 * ILIGNE, "Nathalie Morin");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y9 + ITITRE + 7 * ILIGNE,
                           "Florent and Thomas");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 8 * ILIGNE, "Bruno Singer");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y9 + ITITRE + 9 * ILIGNE,
                           "Francois Deschamps");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 10 * ILIGNE, "Colin Auger");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y9 + ITITRE + 11 * ILIGNE,
                           "TP122 students");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 12 * ILIGNE, "Jeff");
            fnt_rpg.printC(
                backSurface, xcred, y9 + ITITRE + 13 * ILIGNE, "Flavy");

            y10 = y9 + 14 * ILIGNE + IPARTI + ITITRE;

            fnt_rpg.printC(backSurface, xcred, y10, "SPECIAL THANKS TO");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 0 * ILIGNE,
                           "La Guinguette Pirate");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 1 * ILIGNE,
                           "TiPunch power!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 3 * ILIGNE, "La Pirogue");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 4 * ILIGNE,
                           "I love you Séverine!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 6 * ILIGNE, "Le Montbauron");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 7 * ILIGNE,
                           "mind the 8 ball!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 9 * ILIGNE,
                           "La Fleche d'Or");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 10 * ILIGNE,
                           "avoid the toilets!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 12 * ILIGNE,
                           "Hippochine Restaurant");
            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 13 * ILIGNE, "great music!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 15 * ILIGNE,
                           "Vitigno Pizzeria");
            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 16 * ILIGNE, "pizza! good!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 18 * ILIGNE,
                           "Max Linder Theatre");
            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 19 * ILIGNE, "THX rules!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 21 * ILIGNE, "UNEF");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 22 * ILIGNE,
                           "cheap beer 4 all!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 24 * ILIGNE,
                           "Hayao Myazaki");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 25 * ILIGNE,
                           "Totoro! To-to-ro!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 27 * ILIGNE, "Kevin Smith");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 28 * ILIGNE,
                           "thirty seven?");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 30 * ILIGNE,
                           "Peter Jackson");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 31 * ILIGNE,
                           "Bad Taste rules!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 33 * ILIGNE, "Monty Python");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 34 * ILIGNE,
                           "behind the rabbit?");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 36 * ILIGNE, "John Boorman");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 37 * ILIGNE,
                           "Zardoz has spoken!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 39 * ILIGNE, "Luc Besson");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 40 * ILIGNE,
                           "nah... just kidding!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 42 * ILIGNE,
                           "Christopher Lambert");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 43 * ILIGNE,
                           "watch your back!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 45 * ILIGNE, "Jeremy Irons");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 46 * ILIGNE,
                           "lord Profion!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 48 * ILIGNE, "Wayne Scott");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 49 * ILIGNE,
                           "Rambo! Rambo!");

            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 51 * ILIGNE,
                           "Les Joyeux Urbains");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 52 * ILIGNE,
                           "va chez ta mère!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 54 * ILIGNE, "Toss");
            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 55 * ILIGNE, "da! da! da!");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 57 * ILIGNE, "Le cassoulet");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 58 * ILIGNE,
                           "best served at 4 AM");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 60 * ILIGNE, "Gamedev.net");
            fnt_rpg.printC(backSurface,
                           xcred,
                           y10 + ITITRE + 61 * ILIGNE,
                           "DirectX vs hamsters");

            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 64 * ILIGNE, "THANK");
            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 65 * ILIGNE, "YOU");
            fnt_rpg.printC(
                backSurface, xcred, y10 + ITITRE + 66 * ILIGNE, "FOR PLAYING");

            y11 = y10 + 72 * ILIGNE + IPARTI + ITITRE;
        }

        fnt_rpg.printC(backSurface, xcred, y11, "LOADED Studio");
        fnt_rpg.printC(backSurface, xcred, y11 + ILIGNE, "May 2002");

        last_y = y11 + 2 * ILIGNE;

        if (t.elapsed() >= 12500 && theEnd) {
            npage++;
            npage %= pbk_cred.getSize();
            t.Reset();
        }

        DDFlip();
    }
}

//-----------------------------------------------------------------------------
