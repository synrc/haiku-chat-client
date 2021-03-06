/*
 * Copyright 2010-2014, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *                Maxim Sokhatsky <maxim@synrc.com>
 *
 */

#include <Application.h>
#include "RosterView.h"
#include <cstdio>
#include <MenuItem.h>
#include "Settings.h"
#include "Messages.h"
#include "ModalAlertFactory.h"
#include <strings.h>

RosterView::RosterView(BRect frame)
	:
	BOutlineListView(frame, "Roster", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL)
{
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));

}

RosterView::~RosterView()
{
	delete _popup;

	BlabberSettings::Instance()->SetTag("online-collapsed", !_online->IsExpanded());
	BlabberSettings::Instance()->SetTag("unknown-collapsed", !_unknown->IsExpanded());
	BlabberSettings::Instance()->SetTag("offline-collapsed", !_offline->IsExpanded());
	BlabberSettings::Instance()->SetTag("conferences-collapsed", !_conferences->IsExpanded());
	BlabberSettings::Instance()->WriteToFile();
}

static int _ListComparison(const BListItem *a, const BListItem *b)
{
	int return_ = 0;
	
	if (((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == 
			((RosterItem *)b)->GetUserID()->ExactOnlineStatus())
	{
		if (((RosterItem *)a)->GetUserID()->FriendlyName() ==
			((RosterItem *)b)->GetUserID()->FriendlyName())
			return_ = 0;
		else if ( 
			((RosterItem *)a)->GetUserID()->FriendlyName() >
			((RosterItem *)b)->GetUserID()->FriendlyName()) 
			return_ = 1;
		else return_ = -1;
	}
	else if (((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == "none")
	{
		return_ = 1;
	}
	else if (((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == "xa")
	{
		if (((RosterItem *)b)->GetUserID()->ExactOnlineStatus() == "none")
			return_ = -1;
		else
			return_ = 1;
	}
	else if (((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == "away")
	{
		if (((RosterItem *)b)->GetUserID()->ExactOnlineStatus() == "xa" ||
			((RosterItem *)b)->GetUserID()->ExactOnlineStatus() == "none" )
			return_ = -1;
		else 
			return_ = 1;
	}
	else if (((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == "dnd")
	{
		if (((RosterItem *)b)->GetUserID()->ExactOnlineStatus() == "away" ||
			((RosterItem *)b)->GetUserID()->ExactOnlineStatus() == "xa" ||
			((RosterItem *)b)->GetUserID()->ExactOnlineStatus() == "none" )
			return_ = -1;
		else 
			return_ = 1;
	}
	else if (((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == "chat" ||
			((RosterItem *)a)->GetUserID()->ExactOnlineStatus() == "online")
	{
		return_ = -1;
	}
	
	return return_;
}   

void RosterView::AttachedToWindow()
{
	BOutlineListView::AttachedToWindow();
	SetInvocationMessage(new BMessage(JAB_OPEN_CHAT));

	_popup = new BPopUpMenu(NULL, false, false);

	_chat_item         = new BMenuItem("Message...", new BMessage(JAB_OPEN_NEW_CHAT));
	_chat_item->SetEnabled(true);
	_change_user_item  = new BMenuItem("Edit", new BMessage(JAB_OPEN_EDIT_BUDDY_WINDOW));
	_change_user_item->SetEnabled(false);
	_remove_user_item  = new BMenuItem("Remove", new BMessage(JAB_REMOVE_BUDDY));
	_remove_user_item->SetEnabled(false);
	_presence          = new BMenu("Presence");
	_presence->SetEnabled(false);
			
		_subscribe_presence		= new BMenuItem("Subscribe", new BMessage(JAB_SUBSCRIBE_PRESENCE));
		_unsubscribe_presence	= new BMenuItem("Unsubscribe", new BMessage(JAB_UNSUBSCRIBE_PRESENCE));
		_revoke_presence 		= new BMenuItem("Revoke", new BMessage(JAB_REVOKE_PRESENCE));
		_resend_presence		= new BMenuItem("Resend", new BMessage(JAB_RESEND_PRESENCE));

		_presence->AddItem(_subscribe_presence);
		_presence->AddItem(_unsubscribe_presence);
		_presence->AddSeparatorItem();
		_presence->AddItem(_revoke_presence);
		_presence->AddItem(_resend_presence);


	_popup->AddItem(_chat_item);
	_popup->AddSeparatorItem();
	_popup->AddItem(_change_user_item);
	_popup->AddItem(_remove_user_item);
	_popup->AddSeparatorItem();
	_popup->AddItem(_presence);


	UpdatePopUpMenu();

	_online  = new RosterSuperitem("Online");
	_offline = new RosterSuperitem("Offline");
	_unknown = new RosterSuperitem("No Presence");
	_conferences = new RosterSuperitem("Conferences");
	
	_item_to_status_map[_offline] = UserID::OFFLINE;
	_item_to_status_map[_online]  = UserID::ONLINE;
	_item_to_status_map[_unknown] = UserID::UNKNOWN;
	_item_to_status_map[_conferences] = UserID::CONF_STATUS;


	_status_to_item_map[UserID::OFFLINE] = _offline;
	_status_to_item_map[UserID::ONLINE]  = _online;
	_status_to_item_map[UserID::UNKNOWN] = _unknown;
	_status_to_item_map[UserID::CONF_STATUS] = _conferences;
	
	_presence->SetTargetForItems(Window());
	_popup->SetTargetForItems(Window());
	
	CreateRoots();
}

void
RosterView::CreateRoots()
{
	AddItem(_online);
	AddItem(_offline);
	AddItem(_unknown);
	AddItem(_conferences);
	
	_offline->SetExpanded(!BlabberSettings::Instance()->Tag("offline-collapsed"));
	_unknown->SetExpanded(!BlabberSettings::Instance()->Tag("unknown-collapsed"));
	_conferences->SetExpanded(!BlabberSettings::Instance()->Tag("conferences-collapsed"));

}

RosterItem*
RosterView::CurrentItemSelection()
{
	int32 index = CurrentSelection();
	if (index >= 0) return dynamic_cast<RosterItem *>(ItemAt(index));
	else return NULL;
}

void
RosterView::KeyDown(const char *bytes, int32 len)
{
}

void
RosterView::MouseDown(BPoint point)
{
	Window()->Activate(true);
	uint32 buttons = 0;
	GetMouse(&point, &buttons);

	printf("buttons %i\n",buttons);

	if (buttons & B_SECONDARY_MOUSE_BUTTON)
	{
		RosterItem *item = CurrentItemSelection();
		if (item && !item->StalePointer())
		{
			UpdatePopUpMenu();
			BPoint screen_point(point);
			ConvertToScreen(&screen_point);
			BRect r(screen_point.x - 4, screen_point.y - 20, screen_point.x + 24, screen_point.y + 4);
			_popup->Go(screen_point, true, true, r, false);
		}
	} 
	
	BOutlineListView::MouseDown(point);

}

void
RosterView::RemoveSelected()
{
	if (CurrentItemSelection())
	{
		int32       selected = CurrentSelection();
		RosterItem *item     = CurrentItemSelection();
		if (item == NULL) return;
		RemoveItem(CurrentSelection());
		if (ItemAt(selected)) Select(selected);
		else if (ItemAt(selected - 1)) Select(selected - 1);
	}
}

void
RosterView::SelectionChanged()
{
	UpdatePopUpMenu();
	BOutlineListView::SelectionChanged();
}

void RosterView::LinkUser(UserID *added_user, bool sort = true)
{
	if (!added_user) return;
	
	Window()->Lock();
	
	if (added_user->UserType() == UserID::CONFERENCE)
	{
		AddUnder(new RosterItem(added_user), _conferences);
		if (sort) SortItemsUnder(_conferences, true, _ListComparison);
	}
	else if (added_user->UserType() == UserID::JABBER)
	{
		if (added_user->SubscriptionStatus() == "none")
		{
			AddUnder(new RosterItem(added_user), _unknown);
			if (sort) SortItemsUnder(_unknown, true, _ListComparison);
		}
		else 
		{
			if (added_user->OnlineStatus() == UserID::ONLINE)
			{
				AddUnder(new RosterItem(added_user), _online);
				if (sort) SortItemsUnder(_offline, true, _ListComparison);
			}
			else if (added_user->OnlineStatus() == UserID::CONF_STATUS)
			{
				AddUnder(new RosterItem(added_user), _conferences);
				if (sort) SortItemsUnder(_offline, true, _ListComparison);
			}
			else
			{
				AddUnder(new RosterItem(added_user), _offline);
				if (sort) SortItemsUnder(_offline, true, _ListComparison);
			}
		}
	}
	
	Window()->Unlock();
}

void
RosterView::UnlinkUser(UserID *removed_user)
{
	Window()->Lock();
	uint32 index = FindUser(removed_user);
	if (index >= 0) RemoveItem(index);
	Window()->Unlock();
}

int32
RosterView::FindUser(UserID *compare_user)
{
	if (compare_user == NULL) return -1;
	for (int i=0; i<FullListCountItems(); ++i) {
		RosterItem *item = dynamic_cast<RosterItem *>(FullListItemAt(i));
		if (item == NULL || item->StalePointer()) continue;
		if (item->GetUserID() == compare_user) return i;
	}
	return -1;
}

void
RosterView::UpdatePopUpMenu()
{
	RosterItem *item = CurrentItemSelection();

	if (item && !item->StalePointer())
	{
		const UserID *user = item->GetUserID();

		_change_user_item->SetEnabled(true);
		_remove_user_item->SetEnabled(true);
		
		if (user->OnlineStatus() != UserID::CONF_STATUS)
		{
			_presence->SetEnabled(true);
			
			if (user->HaveSubscriptionTo())
			{
				_subscribe_presence->SetEnabled(false);
				_unsubscribe_presence->SetEnabled(true);
			}
			else
			{
				_subscribe_presence->SetEnabled(true);
				_unsubscribe_presence->SetEnabled(false);
			}
		}
		else
		{
			_presence->SetEnabled(false);
		}
	}
	else
	{		
		_change_user_item->SetEnabled(false);
		_remove_user_item->SetEnabled(false);
		_presence->SetEnabled(false);
	}
}

int RosterView::GetConferencesCount()
{
	return CountItemsUnder(_conferences, true);
}

const UserID *RosterView::GetConference(int i)
{
	RosterItem *item = (RosterItem*)ItemUnderAt(_conferences, true, i);
	if (item) return item->GetUserID();
	else return NULL;
}

