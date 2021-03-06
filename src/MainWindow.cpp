/*
 * Copyright 2010-2014, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *                John Blanco
 *                Maxim Sokhatsky <maxim@synrc.com>
 *
 */

#include <cstdio>
#include <Application.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <String.h>
#include <LayoutBuilder.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "MainWindow.h"
#include "BuddyWindow.h"
#include "Messages.h"
#include "ChatWindow.h"
#include "JabberProtocol.h"
#include "Settings.h"
#include "GenericFunctions.h"
#include "MessageRepeater.h"
#include "RosterItem.h"
#include "ModalAlertFactory.h"
#include "TalkManager.h"

#define BOX_WIDTH BSize(fUsername->StringWidth("w")*30, B_SIZE_UNSET)

BlabberMainWindow* BlabberMainWindow::fInstance = NULL;

BlabberMainWindow::~BlabberMainWindow()
{
	MessageRepeater::Instance()->RemoveTarget(this);
	fInstance = NULL;
}

void
BlabberMainWindow::Login()
{
	if (!ValidateLogin()) return;

	fLogin->SetEnabled(false);
		
	UserID username(fUsername->Text());
			
	switch (jabber_code)
	{
		case JAB_COMMON:
					
			jabber->SetConnection(
				BString(username.JabberServer().c_str()), 5223, true);
						
			jabber->SetCredentials(
				BString(username.JabberUsername().c_str()), 
				BString(username.JabberServer().c_str()), BString(fPassword->Text()));
						
			break;
					
		case JAB_FACEBOOK:

			jabber->SetConnection(
				BString("chat.facebook.com"), 5222, false);

			printf("Facebook User Part: '%s'\n", username.JabberHandle().c_str());
										
			jabber->SetCredentials(
				BString(username.JabberHandle().c_str()), 
				BString("facebook.com"), BString(fPassword->Text()));
					
			break;
					
		case JAB_GOOGLE:
		{
			jabber->SetConnection(
				BString("talk.google.com"), 443, true);
					
			printf("Google User Part: '%s'\n", username.JabberUsername().c_str());
			printf("Google Server Part: '%s'\n", username.JabberServer().c_str());
					
			BString serverPart(username.JabberServer().c_str());

			jabber->SetCredentials(
				BString(username.JabberUsername().c_str()), 
				serverPart.IsEmpty() ? BString("gmail.com") : serverPart,
				BString(fPassword->Text()));
			
		}		
			break;
			
		default:
			break;
	}
			
	jabber->LogOn();
}

void
BlabberMainWindow::MessageReceived(BMessage *msg)
{
	static bool reported_info = false;
	
	switch (msg->what) {
		
		case JAB_COMMON:
			jabber_code = msg->what;
			flavour = BString("XMPP");
			fNewAccount->SetEnabled(true);
			break;

		case JAB_FACEBOOK:
			jabber_code = msg->what;
			flavour = BString("Facebook");
			fNewAccount->SetEnabled(false);
			fNewAccount->SetValue(B_CONTROL_OFF);
			break;
			
		case JAB_GOOGLE:
			jabber_code = msg->what;
			flavour = BString("Google");
			fNewAccount->SetEnabled(false);
			fNewAccount->SetValue(B_CONTROL_OFF);
			break;

		case JAB_CONNECT:
		case JAB_LOGIN:
			if (!loginViewCreated) {
				LoginView(true);
			}
			
			if (!mainViewCreated) {
				MainView();
			}
			
			Lock();
			HideLogin();
			SetTitle((string("Chat − ") + UserID(string(jabber->jid.String())).JabberHandle()).c_str());
			fStatusView->SetMessage("gathering agents, roster and presence info");
			Unlock();
			
			Login();
			break;

		case JAB_LOGGED_IN: 
		{
			Lock();
			HideLogin();
			SetTitle((string("Chat − ") + UserID(string(jabber->jid.String())).JabberHandle()).c_str());
			fStatusView->SetMessage("gathering agents, roster and presence info");
			Unlock();
				
			jabber->RequestRoster();
			jabber->SendStorageRequest("storage", "storage:bookmarks");
			
			utsname uname_info;
			if (uname(&uname_info) == 0) {
				char buffer[1000];
				string os_info = uname_info.sysname;
				long revision = 0;
				if (sscanf(uname_info.version, "r%ld", &revision) == 1) {
					char version[16];
					snprintf(version, sizeof(version), "%ld", revision);
					os_info += " (Revision: ";
					os_info += version;
					os_info += ")";
				}
				jabber->SetStatus("online", os_info.c_str());
			}
				
			SaveConfig();
			
			break;
		}
		
		case JAB_DISCONNECT:
			Lock();
			ShowLogin();
			Unlock();
						
			TalkManager::Instance()->Reset();
			jabber->_storage_supported = true;
			jabber->Disconnect();

			break;

		case JAB_OPEN_NEW_CHAT:
		case JAB_OPEN_CHAT:
		{
			uint32 buttons = 0;
			BPoint point(0,0);
			
			fRosterView->GetMouse(&point, &buttons, true);
			printf("JAB_OPEN_CHAT buttons %i\n",buttons);
	
			if (buttons < 1 && (msg->what != JAB_OPEN_NEW_CHAT)) break;
			
			Lock();
			RosterItem *item = fRosterView->CurrentItemSelection();
			
			if (item != NULL)
			{
				TalkManager::Instance()->Lock();
				
				UserID *user = (UserID*)item->GetUserID();
				if (user->UserType() == UserID::CONFERENCE)
				{
					if (!jabber->_storage_supported)
						user->_room_nick = string(BString("__").Append(jabber->user).String());
				
					ChatWindow *window = TalkManager::Instance()->
											CreateTalkSession(ChatWindow::GROUP, user);
				}
				else
					ChatWindow *window = TalkManager::Instance()->
											CreateTalkSession(ChatWindow::CHAT, user);
						
				TalkManager::Instance()->Unlock();

			}
			
			Unlock();
			break;
		}
		
		case JAB_SUBSCRIBE_PRESENCE:
		{
			Lock();
			RosterItem *item = fRosterView->CurrentItemSelection();
			
			if (item != NULL)
			{
				UserID *user = (UserID*)item->GetUserID();
				jabber->SendSubscriptionRequest(user->JabberHandle());
			}
			
			Unlock();
			break;
		}
		
		case JAB_UNSUBSCRIBE_PRESENCE:
		{
			Lock();
			fprintf(stderr, "Sending unsubscribe...");
			RosterItem *item = fRosterView->CurrentItemSelection();
			
			if (item != NULL)
			{
				UserID *user = (UserID*)item->GetUserID();
				jabber->SendUnsubscriptionRequest(user->JabberHandle());
				fprintf(stderr, " for user %s. Done.\n", user->JabberHandle().c_str());
			
			}
			Unlock();
			break;
		}
		
		case JAB_REVOKE_PRESENCE:
		{
			Lock();
			RosterItem *item = fRosterView->CurrentItemSelection();
			
			if (item != NULL) {
				UserID *user = (UserID*)item->GetUserID();
				jabber->RejectPresence(user->JabberHandle());
			}
			Unlock();
			break;
		}
		
		case JAB_RESEND_PRESENCE:
		{
			Lock();
			RosterItem *item = fRosterView->CurrentItemSelection();
			
			if (item != NULL) {
				UserID *user = (UserID*)item->GetUserID();
				jabber->AcceptPresence(user->JabberHandle());
			}
			Unlock();
			break;
		}
		
		case JAB_OPEN_ADD_BUDDY_WINDOW:
			Lock();
			BuddyWindow::Instance()->SetUser(NULL);
			BuddyWindow::Instance()->Show();
			printf("JAB_OPEN_ADD_BUDDY_WINDOW\n");
			Unlock();
			break;
		
		case JAB_OPEN_EDIT_BUDDY_WINDOW:
		{
			Lock();
			RosterItem *item = fRosterView->CurrentItemSelection();
			if (item != NULL) {
				UserID *user = const_cast<UserID *>(item->GetUserID());
				BuddyWindow::Instance()->SetUser(user);
				BuddyWindow::Instance()->Show();
				printf("JAB_OPEN_EDIT_BUDDY_WINDOW\n");
			}
			Unlock();
			break;
		}
		
		case JAB_REMOVE_BUDDY:
		{
			Lock();
			RosterItem *item = fRosterView->CurrentItemSelection();
			UserID *user = NULL;
			
			if (item != NULL)
			{
				user = (UserID *)item->GetUserID();
				fRosterView->fUsers.erase(user->JabberHandle());
				fRosterView->RemoveItem(item);
				fRosterView->Invalidate();
				
				if (user->UserType() == UserID::CONFERENCE && jabber->_storage_supported)
				{
					jabber->SaveConference(NULL);
					jabber->SendStorageRequest("storage", "storage:bookmarks");
				}
				else
					jabber->RemoveFromRoster(user);
				
			}
			Unlock();
			break;
		}
		
		case B_ABOUT_REQUESTED:
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;
		
		case JAB_QUIT:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		
		default:
			BWindow::MessageReceived(msg);
		
	}
}

void
BlabberMainWindow::MenusBeginning()
{
	char buffer[1024];
	if (!fMainView->IsHidden()) {
		fDisconnect->SetEnabled(true);
	} else {
		fDisconnect->SetEnabled(false);
	}
}

void
BlabberMainWindow::SaveConfig()
{
	BlabberSettings::Instance()->SetFloatData("main-window-left", Frame().left);
	BlabberSettings::Instance()->SetFloatData("main-window-top", Frame().top);
	BlabberSettings::Instance()->SetFloatData("main-window-width", Bounds().Width());
	BlabberSettings::Instance()->SetFloatData("main-window-height", Bounds().Height());
	BlabberSettings::Instance()->SetData("last-login", fUsername->Text());
	BlabberSettings::Instance()->SetData("last-password", fPassword->Text());
	BlabberSettings::Instance()->SetTag("auto-login", fAutoLogin->Value());
	BlabberSettings::Instance()->SetData("flavour", flavour.String());
	BlabberSettings::Instance()->WriteToFile();
}

bool
BlabberMainWindow::QuitRequested()
{
	SaveConfig();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void
BlabberMainWindow::FrameResized(float width, float height)
{
	fUsername->Invalidate();
}

void
BlabberMainWindow::MainView(bool hidden=false)
{
	Lock();
	
	fDisconnect = new BMenuItem("Sign-out", new BMessage(JAB_DISCONNECT));
	fDisconnect->SetShortcut('B', 0);
	fAbout = new BMenuItem("About...", new BMessage(B_ABOUT_REQUESTED));
	fAbout->SetTarget(be_app);
	fQuit = new BMenuItem("Quit", new BMessage(JAB_QUIT));
	fQuit->SetShortcut('Q', 0);
	
	fFile = new BMenu("File");
	fFile->AddItem(fDisconnect);
	fFile->AddSeparatorItem();
	fFile->AddItem(fAbout);
	fFile->AddSeparatorItem();
	fFile->AddItem(fQuit);
	fFile->SetTargetForItems(MessageRepeater::Instance());

	fAddBuddy = new BMenuItem("Add Item...", new BMessage(JAB_OPEN_ADD_BUDDY_WINDOW));
	fAddBuddy->SetShortcut('N', 0);
	fPreferences = new BMenuItem("Preferences...", new BMessage(JAB_PREFERENCES));

	fEdit = new BMenu("Roster");
	fEdit->AddItem(fAddBuddy);
	fEdit->AddSeparatorItem();
	fEdit->AddItem(fPreferences);
	fEdit->SetTargetForItems(this);
	
	fMainView = new BView(Bounds(), "main-full", B_FOLLOW_ALL, B_WILL_DRAW);
	if (hidden) fMainView->Hide();
	fMainView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(fMainView);

	fMenuBar = new BMenuBar(Bounds(),"menubar");
	fMenuBar->AddItem(fFile);
	fMenuBar->AddItem(fEdit);
	fMainView->AddChild(fMenuBar);

	fStatusView = new StatusView();
	fStatusView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fStatusView->SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fMainView->AddChild(fStatusView);
	
	BRect rect = Bounds();
	rect.top = fMenuBar->Frame().Height()+1;
	rect.bottom -= B_H_SCROLL_BAR_HEIGHT;
	rect.right -= B_V_SCROLL_BAR_WIDTH;
	fRosterView = new RosterView(rect);
	fRosterScroller = new BScrollView("scroller", fRosterView, B_FOLLOW_ALL_SIDES, 0, false, true);
	fRosterView->TargetedByScrollView(fRosterScroller);
	fMainView->AddChild(fRosterScroller);
	
	mainViewCreated = true;
	
	Unlock();
	
}

void
BlabberMainWindow::LoginView(bool hidden=false)
{
	Lock();
	
	fUsername = new BTextControl("JID: ", "", NULL);
	fUsername->SetEnabled(true);
	fPassword = new BTextControl("Account Password: ", "", NULL);
	fPassword->TextView()->HideTyping(true);
	fNewAccount = new BCheckBox(Bounds(), NULL, "Register Account", NULL, B_FOLLOW_LEFT);
	fNewAccount->SetEnabled(false);
	fAutoLogin = new BCheckBox(Bounds(), NULL, "Auto-login", NULL, B_FOLLOW_LEFT);
	fLogin = new BButton(Bounds(), "login", "Sign-in", new BMessage(JAB_LOGIN), B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
	fLogin->MakeDefault(false);
	fLogin->SetTarget(this);

	fLoginView = new BView(Bounds(), "login-full", B_FOLLOW_ALL, B_WILL_DRAW);
	if (hidden) fLoginView->Hide(); 
	fLoginView->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(fLoginView);
	
	BLayoutItem* right = fUsername->CreateTextViewLayoutItem();
	right->SetExplicitMinSize(BOX_WIDTH);
	
	codes["XMPP"] = JAB_COMMON;
	codes["Facebook"] = JAB_FACEBOOK;
	codes["Google"] = JAB_GOOGLE;
	
	jabber_type = new BPopUpMenu("");
	jabber_type_menu = new BMenuField("", jabber_type);	
	jabber_type->AddItem(new BMenuItem("XMPP", 		new BMessage(JAB_COMMON)));
	jabber_type->AddItem(new BMenuItem("Facebook",	new BMessage(JAB_FACEBOOK)));
	jabber_type->AddItem(new BMenuItem("Google", 	new BMessage(JAB_GOOGLE)));

	if (BlabberSettings::Instance()->Data("flavour"))
	{
		flavour = BString(BlabberSettings::Instance()->Data("flavour"));
		jabber_code = codes[flavour.String()];

		if (jabber_type->FindItem(flavour.String()))
			jabber_type->FindItem(flavour.String())->SetMarked(true);
		
	}
	else
	{
		flavour = BString("Facebook");
		jabber_code = codes[flavour.String()];
		jabber_type->FindItem(flavour.String())->SetMarked(true);
	}
	
	BLayoutBuilder::Group<>(fLoginView, B_VERTICAL, 0)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGlue()
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(jabber_type_menu, 0, 0)
			.Add(fUsername->CreateTextViewLayoutItem(), 1, 0)
			.Add(fPassword->CreateLabelLayoutItem(), 0, 1)
			.Add(fPassword->CreateTextViewLayoutItem(), 1, 1)
			.Add(fNewAccount, 1, 2, 2, 1)
			.Add(fAutoLogin, 1, 3, 2, 1)
		.End()
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fLogin)
		.End()
		.AddGlue()
		.View();

	if (!BlabberSettings::Instance()->Data("last-login")) fUsername->SetText("maxim@synrc.com");
	else fUsername->SetText(BlabberSettings::Instance()->Data("last-login"));
	
	fPassword->SetText(BlabberSettings::Instance()->Data("last-password"));
	fAutoLogin->SetValue(BlabberSettings::Instance()->Tag("auto-login"));
	fPassword->MakeFocus(true);
	
	loginViewCreated = true;
	
	Unlock();
}

BlabberMainWindow::BlabberMainWindow(BRect frame)
	:
	BWindow(frame, "Chat", B_DOCUMENT_WINDOW, 
						   B_ASYNCHRONOUS_CONTROLS)
{
	if (BlabberSettings::Instance()->Tag("auto-login"))  
	{
		MainView();
		PostMessage(JAB_LOGIN);
		loginViewCreated = false;
	}
	else
	{
		LoginView();
		MainView(true);
	}
	
	MessageRepeater::Instance()->AddTarget(this);

}

bool
BlabberMainWindow::ValidateLogin()
{
	if (!strcmp(fUsername->Text(), "")) {
		ModalAlertFactory::Alert("Wait, you haven't specified your Jabber ID yet.\n(e.g. me@jabber.org)",
								 "Doh!", NULL, NULL, B_WIDTH_FROM_LABEL, B_STOP_ALERT);
		fUsername->MakeFocus(true);

		return false;
	}

	UserID username(fUsername->Text());

	if (username.WhyNotValidJabberHandle().size()) {
		char buffer[1024];
		
		if (fNewAccount->Value())
			sprintf(buffer, "The Jabber ID you specified is not legal for the following reason:\n\n%s\n\nYou must specify a legal Jabber ID before you may create a new account.", username.WhyNotValidJabberHandle().c_str());
		else
			sprintf(buffer, "The Jabber ID you specified must not be yours because it's invalid for the following reason:\n\n%s\n\nIf you can't remember it, it's OK to create a new one by checking the \"Create a new Jabber Account!\" box.", username.WhyNotValidJabberHandle().c_str());

		ModalAlertFactory::Alert(buffer, "OK", NULL, NULL, B_WIDTH_FROM_LABEL, B_STOP_ALERT);
		fUsername->MakeFocus(true);

		return false;
	}	

	if (!strcmp(fPassword->Text(), "")) {
		char buffer[1024];

		if (fNewAccount->Value())
			sprintf(buffer, "To create a new account, you must specify a password to protect it, %s.",
					 username.Handle().c_str());
		else
			sprintf(buffer, "You must specify a password so I can make sure it's you, %s.", 
					 username.Handle().c_str());

		ModalAlertFactory::Alert(buffer, "Sorry!", NULL, NULL, B_WIDTH_FROM_LABEL, B_STOP_ALERT);
		fPassword->MakeFocus(true);

		return false;
	}

	return true;
}

BlabberMainWindow*
BlabberMainWindow::Instance()
{
	BlabberSettings *settings = BlabberSettings::Instance();
	
	if (fInstance == NULL && !settings->Data("no-window-on-startup")) 
	{
		float main_window_width, main_window_height;

		if (settings->Data("main-window-width") && settings->Data("main-window-height"))
		{
			main_window_width  = atof(settings->Data("main-window-width"));
			main_window_height = atof(settings->Data("main-window-height"));
		} 
		else
		{
			main_window_width  = 210;
			main_window_height = 332; 
		}
		
		BRect frame(GenericFunctions::CenteredFrame(main_window_width, main_window_height));

		if (settings->Data("main-window-left") && settings->Data("main-window-top"))
		{
			frame.OffsetTo(BPoint(atof(settings->Data("main-window-left")),
									atof(settings->Data("main-window-top"))));
		}

		fInstance = new BlabberMainWindow(frame);
	}
	
	return fInstance;
}

void
BlabberMainWindow::ShowLogin()
{
	int width = ceilf(BOX_WIDTH.width*1.6);
	SetSizeLimits(width, width, 250, 250);
	while (!fMainView->IsHidden()) fMainView->Hide();
	while (fLoginView->IsHidden()) fLoginView->Show();
	fLogin->MakeDefault(true);
	fLogin->SetEnabled(true);
}

void
BlabberMainWindow::HideLogin()
{
	BlabberSettings *settings = BlabberSettings::Instance();
	SetSizeLimits(100, 3000, 200, 3000);
	fLogin->MakeDefault(false);
	fLogin->SetEnabled(false);
	while (!fLoginView->IsHidden()) fLoginView->Hide();
	while (fMainView->IsHidden()) fMainView->Show();
	float main_window_width, main_window_height;
	if (settings->Data("main-window-width") && settings->Data("main-window-height"))
	{
		main_window_width  = atof(settings->Data("main-window-width"));
		main_window_height = atof(settings->Data("main-window-height"));
	} 
	else
	{
		main_window_width  = 210;
		main_window_height = 432; 
	}
	ResizeTo(main_window_width,main_window_height);
}
