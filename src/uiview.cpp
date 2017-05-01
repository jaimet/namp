// uiview.cpp
//
// Copyright (C) 2017 Kristofer Berggren
// All rights reserved.
//
// namp is distributed under the GPLv2 license, see LICENSE for details.
//

#include <QObject>
#include <QTime>
#include <QUrl>
#include <QVector>

#include <locale.h>
#include <wchar.h>

#include <ncursesw/ncurses.h>

#include <fileref.h>
#include <tag.h>

#include "uiview.h"

UIView::UIView(QObject *parent /* = NULL */)
  : QObject(parent)
  , m_TerminalWidth(-1)
  , m_TerminalHeight(-1)
  , m_PlayerWindow(NULL)
  , m_PlaylistWindow(NULL)
  , m_PlayerWindowWidth(40)
  , m_PlayerWindowHeight(6)
  , m_PlayerWindowX(0)
  , m_PlayerWindowY(0)
  , m_PlaylistWindowWidth(40)
  , m_PlaylistWindowMinHeight(6)
  , m_PlaylistWindowHeight(-1)
  , m_PlaylistWindowX(-1)
  , m_PlaylistWindowY(-1)
  , m_TrackPositionSec(0)
  , m_TrackDurationSec(0)
  , m_PlaylistPosition(0)
  , m_PlaylistSelected(0)
  , m_PlaylistOffset(0)
  , m_VolumePercentage(100)
  , m_Shuffle(true)
  , m_UIState(UISTATE_PLAYER)
  , m_PreviousUIState(UISTATE_PLAYER)
  , m_SearchString("")
  , m_SearchStringPos(0)
{
  setlocale(LC_ALL, "");
  initscr();
  noecho();
  Refresh();
}

UIView::~UIView()
{
  wclear(stdscr);
  DeleteWindows();
  endwin();
}

void UIView::PlaylistUpdated(const QVector<QString>& p_Paths)
{
  m_Playlist.clear();
  int index = 0;
  for (const QString& trackPath : p_Paths)
  {
    QString trackName;

    TagLib::FileRef fileRef(trackPath.toStdString().c_str());
    if (!fileRef.isNull() && (fileRef.tag() != NULL))
    {
      TagLib::String artist = fileRef.tag()->artist();
      TagLib::String title = fileRef.tag()->title();
      if ((artist.length() > 0) && (title.length() > 0))
      {
        trackName = QString::fromWCharArray(artist.toWString().c_str()) + " - " + QString::fromWCharArray(title.toWString().c_str());
      }
    }

    if (trackName.isEmpty())
    {
      trackName = QUrl(trackPath).fileName();
    }

    m_Playlist.push_back(TrackInfo(trackPath, trackName, 0, index++));
  }
  Refresh();
}

void UIView::PositionChanged(qint64 p_Position)
{
  m_TrackPositionSec = p_Position / 1000;
  Refresh();
}

void UIView::DurationChanged(qint64 p_Position)
{
  m_TrackDurationSec = p_Position / 1000;
  Refresh();
}

void UIView::CurrentIndexChanged(int p_Position)
{
  m_PlaylistPosition = p_Position;
  m_PlaylistSelected = p_Position;
  Refresh();
}

void UIView::VolumeChanged(int p_Volume)
{
  m_VolumePercentage = p_Volume;
  Refresh();
}

void UIView::PlaybackModeUpdated(bool p_Shuffle)
{
  m_Shuffle = p_Shuffle;
  Refresh();
}

void UIView::Search()
{
  SetUIState(UISTATE_SEARCH);
  Refresh();
}

void UIView::SelectPrevious()
{
  m_PlaylistSelected = qMax(0, m_PlaylistSelected - 1);
  Refresh();
}

void UIView::SelectNext()
{
  m_PlaylistSelected = qMin(m_PlaylistSelected + 1, m_Playlist.count() - 1);
  Refresh();
}

void UIView::PagePrevious()
{
  const int viewMax = m_PlaylistWindowHeight - 2;
  m_PlaylistSelected = qMax(0, m_PlaylistSelected - viewMax);
  Refresh();
}

void UIView::PageNext()
{
  const int viewMax = m_PlaylistWindowHeight - 2;
  m_PlaylistSelected = qMin(m_PlaylistSelected + viewMax, m_Playlist.count() - 1);
  Refresh();
}

void UIView::PlaySelected()
{
  emit SetCurrentIndex(m_PlaylistSelected);
}

void UIView::ToggleWindow()
{
  switch (m_UIState)
  {
    case UISTATE_PLAYER:
      SetUIState(UISTATE_PLAYLIST);
      break;
            
    case UISTATE_PLAYLIST:
      SetUIState(UISTATE_PLAYER);
      break;

    default:
      break;
  }

  Refresh();
}

void UIView::SetUIState(UIState p_UIState)
{
  m_PreviousUIState = m_UIState;
  m_UIState = p_UIState;
  if (m_UIState & UISTATE_SEARCH)
  {
    m_SearchString = "";
    m_SearchStringPos = 0;
    m_PlaylistSelected = 0;
  }
  else
  {
    m_PlaylistSelected = m_PlaylistPosition;
  }

  emit UIStateUpdated(m_UIState);
}

void UIView::Refresh()
{
  UpdateScreen();
  DrawPlayer();
  DrawPlaylist();
}

void UIView::UpdateScreen()
{
  int terminalWidth = -1;
  int terminalHeight = -1;
  getmaxyx(stdscr, terminalHeight, terminalWidth);
  if ((terminalWidth != m_TerminalWidth) || (terminalHeight != m_TerminalHeight))
  {
    m_TerminalWidth = terminalWidth;
    m_TerminalHeight = terminalHeight;

    DeleteWindows();
    CreateWindows();
  }
}

void UIView::DeleteWindows()
{
  wclear(stdscr);

  if (m_PlayerWindow != NULL)
  {
    wclear(m_PlayerWindow);
    delwin(m_PlayerWindow);
    m_PlayerWindow = NULL;
  }

  if (m_PlaylistWindow != NULL)
  {
    wclear(m_PlaylistWindow);
    delwin(m_PlaylistWindow);
    m_PlaylistWindow = NULL;
  }
}

void UIView::CreateWindows()
{
  // Player window has constant size and position
  m_PlayerWindow = newwin(m_PlayerWindowHeight, m_PlayerWindowWidth, m_PlayerWindowY, m_PlayerWindowX);

  if ((m_PlayerWindowHeight + m_PlaylistWindowMinHeight) <= m_TerminalHeight)
  {
    // Playlist can fit under player window (first option)
    m_PlaylistWindowHeight = m_TerminalHeight - m_PlayerWindowHeight;
    m_PlaylistWindowX = 0;
    m_PlaylistWindowY = m_PlayerWindowHeight;
    m_PlaylistWindow = newwin(m_PlaylistWindowHeight, m_PlaylistWindowWidth, m_PlaylistWindowY, m_PlaylistWindowX);
  }
  else if ((m_PlayerWindowWidth + m_PlaylistWindowWidth) <= m_TerminalWidth)
  {
    // Playlist can fit on the right side of player window (second option)
    m_PlaylistWindowHeight = m_PlayerWindowHeight;
    m_PlaylistWindowX = m_PlayerWindowWidth;
    m_PlaylistWindowY = 0;
    m_PlaylistWindow = newwin(m_PlaylistWindowHeight, m_PlaylistWindowWidth, m_PlaylistWindowY, m_PlaylistWindowX);
  }
  else
  {
    // Disable playlist if it cannot fit (last option)
    m_PlaylistWindow = NULL;
  }
}

void UIView::DrawPlayer()
{
  if (m_PlayerWindow != NULL)
  {
    // Border and title
    wclear(m_PlayerWindow);
    wborder(m_PlayerWindow, 0, 0, 0, 0, 0, 0, 0, 0);
    const int titleAttributes = (m_UIState == UISTATE_PLAYER) ? A_BOLD : A_NORMAL;
    wattron(m_PlayerWindow, titleAttributes);
    mvwprintw(m_PlayerWindow, 0, 17, " namp ");
    wattroff(m_PlayerWindow, titleAttributes);

    // Track position
    mvwprintw(m_PlayerWindow, 1, 3, " %02d:%02d", (m_TrackPositionSec / 60), (m_TrackPositionSec % 60));
        
    // Track title
    wchar_t trackName[28] = { 0 };
    mvwprintw(m_PlayerWindow, 1, 11, "%-27s", "");
    swprintf(trackName, 27, L"%s", GetPlayerTrackName(27).toUtf8().constData());
    mvwaddnwstr(m_PlayerWindow, 1, 11, trackName, 27);

    // Volume
    mvwprintw(m_PlayerWindow, 2, 11, "-                   +   PL");
    mvwhline(m_PlayerWindow, 2, 12, 0, (19 * m_VolumePercentage) / 100);

    // Progress
    mvwprintw(m_PlayerWindow, 3, 2, "|                                  |");
    if(m_TrackDurationSec != 0)
    {
      mvwhline(m_PlayerWindow, 3, 3, 0, (34 * m_TrackPositionSec) / m_TrackDurationSec);
    }

    // Playback controls
    mvwprintw(m_PlayerWindow, 4, 2, "|< |> || [] >|  [%c] Shuffle", m_Shuffle ? 'X' : ' ');

    // Refresh
    wrefresh(m_PlayerWindow);
  }
}

QString UIView::GetPlayerTrackName(int p_MaxLength)
{
  QString trackName;
  if (m_PlaylistPosition < m_Playlist.count())
  {
    char position[10];
    snprintf(position, sizeof(position), "(%d:%02d)", (m_TrackDurationSec / 60), (m_TrackDurationSec % 60));
    trackName = m_Playlist.at(m_PlaylistPosition).name + " " + position;
  }
    
  if (trackName.size() > p_MaxLength)
  {
    // Scroll track names that cannot fit
    static QTime lastUpdateTime;
    static int lastPlaylistPosition = -1;
    static int nextUpdateAtElapsed = 0;
    static int trackScrollOffset = 0;

    if (lastPlaylistPosition != m_PlaylistPosition)
    {
      // When track changed, hold title for 4 secs
      lastUpdateTime.start();
      trackScrollOffset = 0;
      nextUpdateAtElapsed = 3900;
      lastPlaylistPosition = m_PlaylistPosition;
    }
    else if (lastUpdateTime.elapsed() > nextUpdateAtElapsed)
    {
      // When timer elapsed, increment scroll position
      lastUpdateTime.start();
      const int maxScrollOffset = trackName.size() - p_MaxLength;
      if (trackScrollOffset < maxScrollOffset)
      {
        // During scroll, view each offset for 1 sec
        nextUpdateAtElapsed = 900;
        ++trackScrollOffset;
      }
      else if (trackScrollOffset == maxScrollOffset)
      {
        // At end of scroll, hold title for 4 secs
        nextUpdateAtElapsed = 3900;
        ++trackScrollOffset;
      }
      else
      {
        trackScrollOffset = 0;
      }
    }

    trackName = trackName.mid(trackScrollOffset, p_MaxLength);
  }
    
  return trackName;
}

void UIView::KeyPress(int p_Key) // can move this to other slots later.
{
  if (ispunct(p_Key) || isalnum(p_Key) || (p_Key == ' '))
  {
    if (m_SearchString.length() < 26)
    {
      m_SearchString.insert(m_SearchStringPos++, QChar(p_Key));
    }
    else
    {
      flash();
    }
  }
  else
  {
    switch (p_Key)
    {
      case '\n':
        if (m_PlaylistSelected < m_Resultlist.length())
        {
          emit SetCurrentIndex(m_Resultlist.at(m_PlaylistSelected).index);
        }
        SetUIState(m_PreviousUIState);
        break;

      case KEY_LEFT:
        m_SearchStringPos = qBound(0, m_SearchStringPos - 1, m_SearchString.length());
        break;

      case KEY_RIGHT:
        m_SearchStringPos = qBound(0, m_SearchStringPos + 1, m_SearchString.length());
        break;

      case KEY_UP:
        m_PlaylistSelected = qBound(0, m_PlaylistSelected - 1, m_Resultlist.count() - 1);
        break;

      case KEY_DOWN:
        m_PlaylistSelected = qBound(0, m_PlaylistSelected + 1, m_Resultlist.count() - 1);
        break;

#ifdef __APPLE__
      case 127:
#endif
      case KEY_BACKSPACE:
        if (m_SearchStringPos > 0)
        {
          m_SearchString.remove(--m_SearchStringPos, 1);
        }
        break;

      case 27:
        SetUIState(m_PreviousUIState);
        break;

      default:
        break;
    }
  }

  Refresh();
}

void UIView::DrawPlaylist()
{
  if (m_PlaylistWindow != NULL)
  {
    // Border
    wclear(m_PlaylistWindow);
    wborder(m_PlaylistWindow, 0, 0, 0, 0, 0, 0, 0, 0);

    if (m_UIState & (UISTATE_PLAYER | UISTATE_PLAYLIST))
    {
      // Title
      const int titleAttributes = (m_UIState & (UISTATE_PLAYLIST | UISTATE_SEARCH)) ? A_BOLD : A_NORMAL;
      wattron(m_PlaylistWindow, titleAttributes);
      mvwprintw(m_PlaylistWindow, 0, 15, " playlist ");
      wattroff(m_PlaylistWindow, titleAttributes);
        
      // Track list
      const int viewMax = m_PlaylistWindowHeight - 2;
      m_PlaylistOffset = qBound(0, (m_PlaylistSelected - ((viewMax - 1) / 2)), qMax(0, m_Playlist.count() - viewMax));
      const int viewCount = qBound(0, m_Playlist.count(), viewMax);
      for (int i = 0; i < viewCount; ++i)
      {
        const int playlistIndex = i + m_PlaylistOffset;
        const int viewLength = m_PlaylistWindowWidth - 3;
        wchar_t trackName[viewLength];
        swprintf(trackName, viewLength, L"%s%-36s", m_Playlist.at(playlistIndex).name.toStdString().c_str(), "");
        wattron(m_PlaylistWindow, (playlistIndex == m_PlaylistSelected) ? A_REVERSE : A_NORMAL);
        mvwaddnwstr(m_PlaylistWindow, i + 1, 2, trackName, viewLength);
        wattroff(m_PlaylistWindow, (playlistIndex == m_PlaylistSelected) ? A_REVERSE : A_NORMAL);
      }
    }
    else
    {
      // Refresh search result list
      m_Resultlist.clear();
      for (const TrackInfo& trackInfo : m_Playlist)
      {
        if (trackInfo.path.contains(m_SearchString, Qt::CaseInsensitive) ||
            trackInfo.name.contains(m_SearchString, Qt::CaseInsensitive))
        {
          m_Resultlist.push_back(trackInfo);
        }
      }

      // Track list
      const int viewMax = m_PlaylistWindowHeight - 2;
      m_PlaylistOffset = qBound(0, (m_PlaylistSelected - ((viewMax - 1) / 2)), qMax(0, m_Resultlist.count() - viewMax));
      const int viewCount = qBound(0, m_Resultlist.count(), viewMax);
      for (int i = 0; i < viewCount; ++i)
      {
        const int playlistIndex = i + m_PlaylistOffset;
        const int viewLength = m_PlaylistWindowWidth - 3;
        wchar_t trackName[viewLength];
        swprintf(trackName, viewLength, L"%s%-36s", m_Resultlist.at(playlistIndex).name.toStdString().c_str(), "");
        wattron(m_PlaylistWindow, (playlistIndex == m_PlaylistSelected) ? A_REVERSE : A_NORMAL);
        mvwaddnwstr(m_PlaylistWindow, i + 1, 2, trackName, viewLength);
        wattroff(m_PlaylistWindow, (playlistIndex == m_PlaylistSelected) ? A_REVERSE : A_NORMAL);
      }
  
      // Title
      wattron(m_PlaylistWindow, A_BOLD);
      mvwprintw(m_PlaylistWindow, 0, 2, " search: %-26s ", m_SearchString.toStdString().c_str());
      wattroff(m_PlaylistWindow, A_BOLD);
      wmove(m_PlaylistWindow, 0, 11 + m_SearchStringPos);
    }

    // Refresh
    wrefresh(m_PlaylistWindow);
  }
}

void UIView::MouseEventRequest(int p_X, int p_Y, uint32_t p_Button)
{
  // Set focus
  if ((p_Y >= m_PlayerWindowY) && (p_Y <= m_PlayerWindowY + m_PlayerWindowHeight) &&
      (p_X >= m_PlayerWindowX) && (p_X <= m_PlayerWindowX + m_PlayerWindowWidth))
  {
    if (m_UIState == UISTATE_PLAYLIST)
    {
      SetUIState(UISTATE_PLAYER);
      Refresh();
    }
  }
  else if ((p_Y >= m_PlaylistWindowY) && (p_Y <= m_PlaylistWindowY + m_PlaylistWindowHeight) &&
           (p_X >= m_PlaylistWindowX) && (p_X <= m_PlaylistWindowX + m_PlaylistWindowWidth))
  {
    if (m_UIState == UISTATE_PLAYER)
    {
      SetUIState(UISTATE_PLAYLIST);
      Refresh();
    }
  }

  // Handle click
  if (p_Button & BUTTON1_CLICKED)
  {
    // Volume
    if ((p_Y == 2) && (p_X >= 11) && (p_X <= 31)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_VOLUME, 100 * (p_X - 11) / 20));

    // Position
    else if ((p_Y == 3) && (p_X >= 2) && (p_X <= 37)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_POSITION, 100 * (p_X - 2) / 35));

    // Previous
    else if ((p_Y == 4) && (p_X >= 2) && (p_X <= 3)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_PREVIOUS, 0));
        
    // Play
    else if ((p_Y == 4) && (p_X >= 5) && (p_X <= 6)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_PLAY, 0));
        
    // Pause
    else if ((p_Y == 4) && (p_X >= 8) && (p_X <= 9)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_PAUSE, 0));

    // Stop
    else if ((p_Y == 4) && (p_X >= 11) && (p_X <= 12)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_STOP, 0));

    // Next
    else if ((p_Y == 4) && (p_X >= 14) && (p_X <= 15)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_NEXT, 0));

    // Shuffle
    else if ((p_Y == 4) && (p_X >= 18) && (p_X <= 20)) emit ProcessMouseEvent(UIMouseEvent(UIELEM_SHUFFLE, 0));

    // Playlist
    else if ((p_Y > m_PlaylistWindowY) && (p_Y < (m_PlaylistWindowY + m_PlaylistWindowHeight)) &&
             (p_X > (m_PlaylistWindowX + 1)) && (p_X < (m_PlaylistWindowX + m_PlaylistWindowWidth - 1)))
    {
      m_PlaylistSelected = m_PlaylistOffset + p_Y - m_PlaylistWindowY - 1;
      Refresh();
    }
  }

  // Handle double click
  if (p_Button & BUTTON1_DOUBLE_CLICKED)
  {
    // Playlist
    if ((p_Y > m_PlaylistWindowY) && (p_Y < (m_PlaylistWindowY + m_PlaylistWindowHeight)) &&
        (p_X > (m_PlaylistWindowX + 1)) && (p_X < (m_PlaylistWindowX + m_PlaylistWindowWidth - 1)))
    {
      m_PlaylistSelected = m_PlaylistOffset + p_Y - m_PlaylistWindowY - 1;
      Refresh();
      emit SetCurrentIndex(m_PlaylistSelected);
    }
  }

  // Handle scroll down
#ifdef __APPLE__
  if (p_Button & 0x00200000)
#else
  if (p_Button & 0x08000000)
#endif
  {
    if (m_UIState == UISTATE_PLAYER)
    {
      emit ProcessMouseEvent(UIMouseEvent(UIELEM_VOLUMEDOWN, 0));
    }
    else
    {
      m_PlaylistSelected = qBound(0, m_PlaylistSelected + 1, m_Playlist.count() - 1);
      Refresh();
    }
  }

  // Handle scroll up
#ifdef __APPLE__
  if (p_Button & 0x00010000)
#else
  if (p_Button & 0x00080000)
#endif
  {
    if (m_UIState == UISTATE_PLAYER)
    {
      emit ProcessMouseEvent(UIMouseEvent(UIELEM_VOLUMEUP, 0));
    }
    else
    {
      m_PlaylistSelected = qBound(0, m_PlaylistSelected - 1, m_Playlist.count() - 1);
      Refresh();
    }
  }
}
