/*
 * LaosMenu.cpp
 * Menu structure and user interface. Uses LaosDisplay
 *
 * Copyright (c) 2011 Peter Brier & Jaap Vermaas
 *
 *   This file is part of the LaOS project (see: http://wiki.laoslaser.org/)
 *
 *   LaOS is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   LaOS is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with LaOS.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "LaosMenu.h"

#include "pins.h"
#include "stepper.h"

static const char *menus[] = {
    "STARTUP",          // 0
    "MAIN",             // 1
    "START JOB",        // 2
    "BOUNDARIES",       // 3
    "DELETE JOB",       // 4
    "HOME",             // 5
    "MOVE",             // 6
    "FOCUS",            // 7
    "ORIGIN",           // 8
    "REMOVE ALL JOBS",  // 9
    "IP",               // 10
    "REBOOT",           // 11
    "LASER TEST",       // 12
    // "POWER / SPEED",//12
    // "IO", //13
};

static const char *screens[] = {
// 0: main, navigate to  MOVE, FOCUS, HOME, ORIGIN, START JOB, IP,
//  DELETE JOB, POWER
#define STARTUP (0)
    "$$$$$$$$$$$$$$$$"
    "$$$$$$$$$$$$$$$$",

#define MAIN (STARTUP + 1)
    "$$$$$$$$$$$$$$$$"
    "<----- 10 ----->",

#define RUN (MAIN + 1)
    "RUN:            "
    "$$$$$$$$$$$$$$$$",

#define BOUNDARIES (RUN + 1)
    "BOUNDARIES:     "
    "$$$$$$$$$$$$$$$$",

#define DELETE (BOUNDARIES + 1)
    "DELETE:         "
    "$$$$$$$$$$$$$$$$",

#define HOME (DELETE + 1)
    "HOME?           "
    "      [ok]      ",

#define MOVE (HOME + 1)
    "X: +6543210 MOVE"
    "Y: +6543210     ",

#define FOCUS (MOVE + 1)
    "Z: +543210 FOCUS"
    "                ",

#define ORIGIN (FOCUS + 1)
    "  SET ORIGIN?   "
    "      [ok]      ",

#define DELETE_ALL (ORIGIN + 1)
    "DELETE ALL FILES"
    "      [ok]      ",

#define IP (DELETE_ALL + 1)
    "210.210.210.210 "
    "$$$$$$$$[ok]    ",

#define REBOOT (IP + 1)
    "REBOOTING...    "
    "Please wait...  ",

#define LASERTEST (REBOOT + 1)
    "LASER TEST:     "
    "210ms 210%      ",

#define POWER (LASERTEST + 1)
    "$$$$$$$: 6543210"
    "      [ok]      ",

#define IO (POWER + 1)
    "$$$$$$$$$$$=0 IO"
    "      [ok]      ",

// Intermediate screens
#define DELETE_OK (IO + 1)
    "DELETE 10?      "
    "      [ok]      ",

#define HOMING (DELETE_OK + 1)
    "HOMING...       "
    "                ",

#define ANALYZING (HOMING + 1)
    "ANALYZING...    "
    "                ",

#define RUNNING (ANALYZING + 1)
    "RUNNING...      "
    "[cancel]        ",

#define BUSY (RUNNING + 1)
    "BUSY: $$$$$$$$$$"
    "[cancel][ok]    ",

#define PAUSE (BUSY + 1)
    "PAUSE: $$$$$$$$$"
    "[cancel][ok]    ",

#define CALCULATEDBOUNDARIES (PAUSE + 1)
    "Or: +3210, +3210"
    "Siz: 3210 x 3210",

#define ERROR (CALCULATEDBOUNDARIES + 1)
    "ERROR:          "
    "$$$$$$$$$$$$$$$$",

};

static const char *ipfields[] = {"IP", "NETMASK", "GATEWAY", "DNS"};
// static  const char *powerfields[] = { "Pmin %", "Pmax %", "Voff", "Von" };
// static  const char *iofields[] = { "o1:PURGE", "o2:EXHAUST", "o3:PUMP", "i1:COVER", "i2:PUMPOK", "i3:LASEROK", "i4:PURGEOK" };

/**
*** Make new menu object
**/
LaosMenu::LaosMenu(LaosDisplay *display) {
  waitup = timeout = iofield = ipfield = 0;
  sarg = NULL;
  screen = prevscreen = lastscreen = speed = 0;
  menu = 1;
  strcpy(jobname, "");
  dsp = display;
  if (dsp == NULL) dsp = new LaosDisplay();
  dsp->cls();
  SetScreen("");
  runfile = NULL;
  m_LaserTestPower = 0;
  m_LaserTestTime = 0;
}

/**
*** Destroy menu object
**/
LaosMenu::~LaosMenu() {
}

/**
*** Goto specific screen
**/
void LaosMenu::SetScreen(int screen) {
  sarg = NULL;
  this->screen = screen;
  Handle();
  Handle();
  Handle();
}

/**
*** Goto specific screen
**/
void LaosMenu::SetScreen(const std::string &msg) {
  if (msg.size() == 0) {
    sarg = NULL;
    screen = MAIN;
    // } else if ( msg[0] == 0 ) {
    //    screen = MAIN;
  } else {
    sarg = new char[msg.size() + 1];
    strcpy(sarg, msg.c_str());
    screen = STARTUP;
  }
  prevscreen = -1;  // force update
  Handle();
  Handle();
  Handle();
}

/***
 *** check if cancel is pressed
 **/
bool LaosMenu::Cancel() {
  int c = dsp->read();
  return (c == K_CANCEL);
}

/**
*** Handle menu system
*** Read keys, and plan next action on the screen, output screen if
*** something changed
**/
void LaosMenu::Handle() {
  //    int xt, yt, zt,
  int nodisplay = 0;
  extern LaosFileSystem sd;
  extern LaosMotion *mot;
  extern GlobalConfig *cfg;
  static int count = 0;

  int c = dsp->read();
  if (count++ > 10) count = 0;  // screen refresh counter (refresh once every 10 cycles(

  if (c)
    timeout = 10;  // keypress timeout counter
  else if (timeout)
    timeout--;

  if (screen != prevscreen) waitup = 1;  // after a screen change: wait for a key release, mask current keypress
  if (waitup && timeout)                 // if we have to wait for key-up,
    c = 0;                               // cancel the keypress
  if (waitup && !timeout) waitup = 0;

  if (!timeout) {
    // keyboard timeout (i.e. all keys are released):
    // reset jog/focus speed:
    speed = 3;
  }

  if ((c != 0) || (timeout == 0)) {
    if (m_PrevKey != c) {
      // and when starting a new move, wait till the previous movements are completely finished.
      // This is needed, otherwise back-and-forth moves could result in a far move being
      // done at a low speed, which would take a long time to finish:
      m_MoveWaitTillQueueEmpty = true;
    }
    m_PrevKey = c;
  }

  if (c || screen != prevscreen || count > 9) {
    switch (screen) {
      case STARTUP:
        if (sarg == NULL) sarg = (char *)VERSION_STRING;
        break;
      case MAIN:
        switch (c) {
          case K_RIGHT:
            menu += 1;
            waitup = 1;
            break;
          case K_LEFT:
            menu -= 1;
            waitup = 1;
            break;
          case K_UP:
            lastscreen = MAIN;
            screen = MOVE;
            menu = MAIN;
            break;
          case K_DOWN:
            lastscreen = MAIN;
            screen = MOVE;
            menu = MAIN;
            break;
          case K_OK:
            screen = menu;
            waitup = 1;
            lastscreen = MAIN;
            break;
          case K_CANCEL:
            menu = MAIN;
            break;
          case K_FUP:
            lastscreen = MAIN;
            screen = FOCUS;
            menu = MAIN;
            break;
          case K_FDOWN:
            lastscreen = MAIN;
            screen = FOCUS;
            menu = MAIN;
            break;
          case K_ORIGIN:
            lastscreen = MAIN;
            screen = ORIGIN;
            waitup = 1;
            break;
        }
        if (menu == 0) menu = (sizeof(menus) / sizeof(menus[0])) - 1;
        if (menu == (sizeof(menus) / sizeof(menus[0]))) menu = 1;
        sarg = (char *)menus[menu];
        args[0] = menu;
        break;

      case RUN:  // START JOB select job to run
        if (strlen(jobname) == 0) getprevjob(jobname);
        switch (c) {
          case K_OK:
            screen = ANALYZING;
            m_StageAfterAnalyzing = RUNNING;
            break;
          case K_UP:
          case K_FUP:
            getprevjob(jobname);
            waitup = 1;
            break;  // next job
          case K_RIGHT:
            screen = BOUNDARIES;
            waitup = 1;
            break;
          case K_DOWN:
          case K_FDOWN:
            getnextjob(jobname);
            waitup = 1;
            break;  // prev job
          case K_CANCEL:
            screen = 1;
            waitup = 1;
            break;
        }
        sarg = (char *)&jobname;
        break;

      case DELETE:  // DELETE JOB select job to run
        switch (c) {
          case K_OK:
            removefile(jobname);
            screen = lastscreen;
            waitup = 1;
            break;  // INSERT: delete current job
          case K_UP:
          case K_FUP:
            getprevjob(jobname);
            waitup = 1;
            break;  // next job
          case K_DOWN:
          case K_FDOWN:
            getnextjob(jobname);
            waitup = 1;
            break;  // prev job
          case K_LEFT:
            screen = BOUNDARIES;
            waitup = 1;
            break;
          case K_CANCEL:
            screen = lastscreen;
            waitup = 1;
            break;
        }
        sarg = (char *)&jobname;
        break;

      case MOVE:  // pos xy
      {
        int x, y, z;
        int numinqueue = mot->queue();
        mot->getCurrentPositionRelativeToOrigin(&x, &y, &z);
        int xt = x;
        int yt = y;
        switch (c) {
          case K_DOWN:
            y -= 100 * speed;
            break;
          case K_UP:
            y += 100 * speed;
            break;
          case K_LEFT:
            x -= 100 * speed;
            break;
          case K_RIGHT:
            x += 100 * speed;
            break;
          case K_OK:
          case K_CANCEL:
            screen = MAIN;
            waitup = 1;
            break;
            //                        case K_FUP: screen=FOCUS; break;
          case K_FUP:
            // use the Focus Up button to display debugging data for the stepper interrupt:
            st_debug();
            screen = FOCUS;
            break;
          case K_FDOWN:
            screen = FOCUS;
            break;
          case K_ORIGIN:
            screen = ORIGIN;
            break;
        }
        printf("Move: c: %d, numinqueue: %d, xt: %d, yt: %d,  waitempty: %d\n",
               c, numinqueue, xt, yt, m_MoveWaitTillQueueEmpty ? 1 : 0);
        int maxinqueue = m_MoveWaitTillQueueEmpty ? 1 : 5;

        /*
           Currently (Jan 2015) an old version of grbl is used in Laos. This version is buggy
           in the sense that there is no proper synchronization between the stepper interrupt and the
           main thread. When adding a new move to the queue through plan_buffer_line(), planner_recalculate()
           is called. This updates the acceleration profile of existing moves in the queue,
           potentially including the move which is currently being executed by the stepper interrupt.
           Due to the lack of synchronization this will lead to problems, including potential crashes or
           hanging. This problem in particular manifests itself during jogging, because the queue is nearly
           emtpy and there's a big chance the frontmost item in the stepper queue will be updated.
           During regular laser cutting the problem is less likely to occur since the queue will continuously
           be kept nearly full. Recalculation will only affect some of the last moves in the queue.

           The real fix for this bug would be to update grbl to the latest version, this seems to include
           proper synchronization. For now we work around the problem by wating for the queue to empty
           before adding new jog moves. This results in sloppy jogging (continuous stops & moves) but at
           least it doesn't crash the machine.
           -joostn
        */

        // always wait till queue is emptied:
        // just remove this line if grbl ever gets updated:
        maxinqueue = 1;

        if ((numinqueue < maxinqueue) && ((x != xt) || (y != yt))) {
          m_MoveWaitTillQueueEmpty = false;
          mot->moveToRelativeToOrigin(x, y, z, speed);
          printf("Move: %d %d %d %d\n", x, y, z, speed);
          speed = speed * 3;
          if (speed > 100) speed = 100;
        } else {
          // if (! mot->ready())
          // printf("Buffer vol\n");
        }
        args[0] = x;
        args[1] = y;
      } break;

      case FOCUS:  // focus
      {
        int x, y, z;
        mot->getCurrentPositionRelativeToOrigin(&x, &y, &z);
        int zt = z;
        switch (c) {
          case K_FUP:
            z += cfg->zspeed * speed;
            if (z > cfg->zmax) z = cfg->zmax;
            break;
          case K_FDOWN:
            z -= cfg->zspeed * speed;
            if (z < 0) z = 0;
            break;
          case K_LEFT:
            break;
          case K_RIGHT:
            break;
          case K_UP:
            z += cfg->zspeed * speed;
            if (z > cfg->zmax) z = cfg->zmax;
            break;
          case K_DOWN:
            z -= cfg->zspeed * speed;
            if (z < 0) z = 0;
            break;
          case K_ORIGIN:
            screen = ORIGIN;
            break;
          case K_OK:
          case K_CANCEL:
            screen = MAIN;
            waitup = 1;
            break;
          case 0:
            break;
          default:
            screen = MAIN;
            waitup = 1;
            break;
        }
        int numinqueue = mot->queue();
        if (mot->ready() && (z != zt) && (numinqueue == 0)) {
          mot->moveToRelativeToOrigin(x, y, z, speed);
          printf("Focus: %d %d %d %d\n", x, y, z, speed);
          speed = speed * 3;
          if (speed > 100) speed = 100;
        }
        args[0] = z;
      } break;

      case HOME:  // home
        switch (c) {
          case K_OK:
            screen = HOMING;
            break;
          case K_CANCEL:
            screen = MAIN;
            menu = MAIN;
            waitup = 1;
            break;
        }
        break;

      case ERROR:
        switch (c) {
          case K_OK:
          case K_CANCEL:
            screen = MAIN;
            waitup = 1;
            break;
        }
        break;

      case ORIGIN:  // origin
        switch (c) {
          case K_CANCEL:
            screen = MAIN;
            menu = MAIN;
            waitup = 1;
            break;
          case K_OK:
          case K_ORIGIN:
            if (cfg->BedHeight() == 0) {
              screen = ERROR;
              sarg = (char *)"bedheight unknwn";
            } else {
              mot->MakeCurrentPositionOrigin();
              screen = lastscreen;
            }
            waitup = 1;
            break;
        }
        break;

      case DELETE_ALL:  // Delete all files
        switch (c) {
          case K_OK:  // delete current job
            cleandir();
            screen = MAIN;
            waitup = 1;
            strcpy(jobname, "");
            break;
          case K_CANCEL:
            screen = MAIN;
            waitup = 1;
            break;
        }
        break;

      case IP:  // IP
        int myip[4], mynm[4], mygw[4], mydns[4];
        IpParse(cfg->ip, myip);
        IpParse(cfg->nm, mynm);
        IpParse(cfg->gw, mygw);
        IpParse(cfg->dns, mydns);
        switch (c) {
          case K_RIGHT:
            ipfield++;
            waitup = 1;
            break;
          case K_LEFT:
            ipfield--;
            waitup = 1;
            break;
          case K_OK:
            screen = MAIN;
            menu = MAIN;
            break;
          case K_CANCEL:
            screen = MAIN;
            menu = MAIN;
            break;
        }
        ipfield %= 4;
        sarg = (char *)ipfields[ipfield];
        switch (ipfield) {
          case 0:
            memcpy(args, myip, 4 * sizeof(int));
            break;
          case 1:
            memcpy(args, mynm, 4 * sizeof(int));
            break;
          case 2:
            memcpy(args, mygw, 4 * sizeof(int));
            break;
          case 3:
            memcpy(args, mydns, 4 * sizeof(int));
            break;
          default:
            memset(args, 0, 4 * sizeof(int));
            break;
        }
        break;

      case REBOOT:  // RESET MACHINE
        mbed_reset();
        break;

        /*
                    case IO: // IO
                        switch ( c ) {
                            case K_RIGHT: iofield++; waitup=1; break;
                            case K_LEFT: iofield--; waitup=1; break;
                            case K_OK: screen=lastscreen; break;
                            case K_CANCEL: screen=lastscreen; break;
                        }
                        iofield %= sizeof(iofields)/sizeof(char*);
                        sarg = (char*)iofields[iofield];
                        args[0] = ipfield;
                        args[1] = ipfield;
                        break;

                    case POWER: // POWER
                        switch ( c ) {
                            case K_RIGHT: powerfield++; waitup=1; break;
                            case K_LEFT: powerfield--; waitup=1; break;
                            case K_UP: power[powerfield % 4] += speed; break;
                            case K_DOWN: power[powerfield % 4] -= speed; break;
                            case K_OK: screen=lastscreen; break;
                            case K_CANCEL: screen=lastscreen; break;
                        }
                        powerfield %= 4;
                        args[1] = powerfield;
                        sarg = (char*)powerfields[powerfield];
                        args[0] = power[powerfield];
                        break;
        */
      case HOMING:  // Homing screen
        while (!mot->isStart())
          ;
        mot->home(cfg->xhome, cfg->yhome, cfg->zhome);
        screen = lastscreen;
        break;

      case RUNNING:  // Screen while running
        switch (c) {
          /* case K_CANCEL:
              while (mot->queue());
              mot->reset();
              if (runfile != NULL) fclose(runfile);
              runfile=NULL; screen=MAIN; menu=MAIN;
              break; */
          default:
            if (runfile == NULL) {
              runfile = sd.openfile(jobname, "rb");
              if (!runfile)
                screen = MAIN;
              else
                mot->reset();
            } else {
#ifdef READ_FILE_DEBUG
              printf("Parsing file: \n");
#endif
              while ((!feof(runfile)) && mot->ready()) {
                mot->write(readint(runfile));
                if (cfg->disablecancelcheck == false) {
                  if (dsp->read_nb() == K_CANCEL) {
                    while (mot->queue())
                      ;
                    mot->reset();
                    fseek(runfile, 0, SEEK_END);
                  }
                }
              }
#ifdef READ_FILE_DEBUG
              printf("File parsed \n");
#endif
              if (feof(runfile) && mot->ready()) {
                fclose(runfile);
                runfile = NULL;
                mot->moveToAbsolute(cfg->xrest, cfg->yrest, cfg->zrest);
                screen = MAIN;
              } else {
                nodisplay = 1;
              }
            }
        }
        break;

      case BOUNDARIES:
        if (strlen(jobname) == 0) getprevjob(jobname);
        switch (c) {
          case K_OK:
            screen = ANALYZING;
            m_StageAfterAnalyzing = CALCULATEDBOUNDARIES;
            break;
          case K_UP:
          case K_FUP:
            getprevjob(jobname);
            waitup = 1;
            break;  // next job
          case K_DOWN:
          case K_FDOWN:
            getnextjob(jobname);
            waitup = 1;
            break;  // prev job
          case K_LEFT:
            screen = RUN;
            waitup = 1;
            break;
          case K_RIGHT:
            screen = DELETE;
            waitup = 1;
            break;
          case K_CANCEL:
            screen = lastscreen;
            waitup = 1;
            break;
        }
        sarg = (char *)&jobname;
        break;

      case ANALYZING:  // determine the boundaries of the file. This is done prior to running, and when executing the BOUNDAIES function
        // m_StageAfterAnalyzing is set to the menu stage that will be executed after this (RUNNING or CALCULATEDBOUNDARIES)
        runfile = sd.openfile(jobname, "rb");
        if (!runfile) {
          screen = MAIN;
        } else {
          // when running we need the bounds including all moves
          // when executing BOUNDARIES we only need the actual lasered area
          bool boundsOnlyWithLaserOn = (m_StageAfterAnalyzing == CALCULATEDBOUNDARIES);
          m_Extent.Reset(boundsOnlyWithLaserOn);
          while (!feof(runfile)) {
            m_Extent.Write(readint(runfile));
          }
          fclose(runfile);
          runfile = NULL;
          int fileMinx, fileMiny, fileMaxx, fileMaxy;
          LaosExtent::TError err = m_Extent.GetBoundary(fileMinx, fileMiny, fileMaxx, fileMaxy);
          bool outOfBounds = false;
          if (!err) {
            int minx, miny, minz, maxx, maxy, maxz;
            mot->getLimitsRelative(&minx, &miny, &minz, &maxx, &maxy, &maxz);
            if ((fileMinx < minx) || (fileMiny < miny) ||
                (fileMaxx > maxx) || (fileMaxy > maxy)) {
              outOfBounds = true;
            }
          }
          if (outOfBounds) {
            screen = ERROR;
            sarg = (char *)"Limit overrun";
            waitup = 1;
          } else {
            if (m_StageAfterAnalyzing == CALCULATEDBOUNDARIES) {
              args[0] = (fileMinx + 500) / 1000;
              args[1] = (fileMiny + 500) / 1000;
              args[2] = ((fileMaxx - fileMinx) + 500) / 1000;
              args[3] = ((fileMaxy - fileMiny) + 500) / 1000;
              screen = CALCULATEDBOUNDARIES;
              m_SubStage = 0;
            } else if (m_StageAfterAnalyzing == RUNNING) {
              screen = RUNNING;
            } else {
              screen = MAIN;
            }
            waitup = 1;
          }
        }
        break;

      case CALCULATEDBOUNDARIES:  // Screen after calculating the boundaries of a file
        if (m_SubStage == 0) {
          m_Extent.ShowBoundaries(mot);
          m_SubStage++;
        } else {
          switch (c) {
            case K_OK:
            case K_CANCEL:
              screen = MAIN;
              break;
          }
          break;
        }
        break;

      case LASERTEST:
        enable = !cfg->enable;
        switch (c) {
          case K_OK:
            waitup = 1;
            if (m_LaserTestTime > 0) {
              double p = (double)(cfg->pwmmin / 100.0 + ((m_LaserTestPower / 100.0) * ((cfg->pwmmax - cfg->pwmmin) / 100.0)));
              pwm = p;
              *laser = LASERON;
              wait_ms(m_LaserTestTime);
              *laser = LASEROFF;
              pwm = cfg->pwmmax / 100.0;  // set pwm to max;
            }
            break;
          case K_UP:
          case K_FUP:
            if (m_LaserTestPower <= 3) {
              m_LaserTestPower++;
            } else {
              m_LaserTestPower += (m_LaserTestPower >> 1);
              if (m_LaserTestPower > 100) m_LaserTestPower = 100;
            }
            break;
          case K_DOWN:
          case K_FDOWN:
            if (m_LaserTestPower <= 4) {
              m_LaserTestPower--;
              if (m_LaserTestPower < 0) m_LaserTestPower = 0;
            } else {
              m_LaserTestPower -= (m_LaserTestPower >> 2);
            }
            break;
          case K_RIGHT:
            if (m_LaserTestTime <= 3) {
              m_LaserTestTime++;
            } else {
              m_LaserTestTime += (m_LaserTestTime >> 1);
              if (m_LaserTestTime > 250) m_LaserTestTime = 250;
            }
            break;
          case K_LEFT:
            if (m_LaserTestTime <= 4) {
              m_LaserTestTime--;
              if (m_LaserTestTime < 0) m_LaserTestTime = 0;
            } else {
              m_LaserTestTime -= (m_LaserTestTime >> 2);
            }
            break;
          case K_CANCEL: {
            enable = cfg->enable;
            // home the machine:
            while (!mot->isStart())
              ;
            mot->home(cfg->xhome, cfg->yhome, cfg->zhome);
            screen = MAIN;
            m_LaserTestPower = 0;
            m_LaserTestTime = 0;
            waitup = 1;
          } break;
        }
        args[0] = m_LaserTestTime;
        args[1] = m_LaserTestPower;
        break;

      default:
        screen = MAIN;
        break;
    }
    if (nodisplay == 0) {
      dsp->ShowScreen(screens[screen], args, sarg);
    }
    prevscreen = screen;
  }
}

void LaosMenu::SetFileName(char *name) {
  strcpy(jobname, name);
}
