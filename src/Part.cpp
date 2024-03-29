/*
    Copyright 2007-2008 by Robert Knight <robertknight@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/

// Own
#include "Part.h"

// Qt
#include <QtCore/QStringList>
#include <QtCore/QDir>

// KDE
#include <KAction>
#include <KActionCollection>
#include <KDebug>
#include <KLocale>
#include <KWindowSystem>
#include <KPluginFactory>
#include <kde_file.h>

// Konsole
#include "ColorScheme.h"
#include "EditProfileDialog.h"
#include "Emulation.h"
#include "KeyboardTranslator.h"
#include "ManageProfilesDialog.h"
#include "Session.h"
#include "SessionController.h"
#include "SessionManager.h"
#include "TerminalDisplay.h"
#include "ViewManager.h"
#include "MainWindow.h"

#include <config-konsole.h>

using namespace Konsole;

K_PLUGIN_FACTORY(KonsolePartFactory, registerPlugin<Konsole::Part>();)
K_EXPORT_PLUGIN(KonsolePartFactory("konsole"))


Part::Part(QWidget* parentWidget , QObject* parent, const QVariantList&)
 : KParts::ReadOnlyPart(parent)
  ,_viewManager(0)
  ,_pluggedController(0)
  ,_manageProfilesAction(0)
{
    // make sure the konsole catalog is loaded
    KGlobal::locale()->insertCatalog("konsole");
    // make sure the libkonq catalog is loaded( needed for drag & drop )
    KGlobal::locale()->insertCatalog("libkonq");

    // setup global actions
    createGlobalActions();

    // create view widget
    _viewManager = new ViewManager(this,actionCollection());
    _viewManager->setNavigationMethod( ViewManager::NoNavigation );

    connect( _viewManager , SIGNAL(activeViewChanged(SessionController*)) , this ,
           SLOT(activeViewChanged(SessionController*)) );
    connect( _viewManager , SIGNAL(empty()) , this , SLOT(terminalExited()) );
    connect( _viewManager , SIGNAL(newViewRequest()) , this , SLOT(newTab()) );

    _viewManager->widget()->setParent(parentWidget);

    setWidget(_viewManager->widget());
    actionCollection()->addAssociatedWidget(_viewManager->widget());
    foreach (QAction* action, actionCollection()->actions())
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);

    // Enable translucency support.
    _viewManager->widget()->setAttribute(Qt::WA_TranslucentBackground, true);
    TerminalDisplay::HAVE_TRANSPARENCY = KWindowSystem::compositingActive();

    // create basic session
    createSession();
}
Part::~Part()
{
    SessionManager::instance()->saveSettings();
}
void Part::createGlobalActions()
{
    _manageProfilesAction = new QAction(i18n("Manage Profiles..."),this);
    connect(_manageProfilesAction,SIGNAL(triggered()),this,SLOT(showManageProfilesDialog()));
}
void Part::setupActionsForSession(SessionController* session)
{
    // TODO: what meaningful operation should be placed here?
    Q_UNUSED(session);
    connect(
        session->session()->emulation(), SIGNAL(receiveLine(const QString&)),
        this, SIGNAL(receiveLine(const QString&))
    );
}
bool Part::openFile()
{
    return false;
}
void Part::terminalExited()
{
    deleteLater();
}
void Part::newTab()
{
    createSession();
    showShellInDir( QString() );
}
Session* Part::activeSession() const
{
    if ( _viewManager->activeViewController() )
    {
        Q_ASSERT( _viewManager->activeViewController()->session());

        return _viewManager->activeViewController()->session();
    }
    else
    {
        return 0;
    }
}
void Part::startProgram( const QString& program,
                           const QStringList& arguments )
{
    Q_ASSERT( activeSession() );

    if ( !activeSession()->isRunning() )
    {
        if ( !program.isEmpty() && !arguments.isEmpty() )
        {
            activeSession()->setProgram(program);
            activeSession()->setArguments(arguments);
        }

        activeSession()->run();
    }
}
void Part::openTeletype(int fd)
{
    Q_ASSERT( activeSession() );

    activeSession()->openTeletype(fd);
}
void Part::showShellInDir( const QString& dir )
{
    Q_ASSERT( activeSession() );

    if ( !activeSession()->isRunning() )
    {
        if ( !dir.isEmpty() )
            activeSession()->setInitialWorkingDirectory(dir);
        activeSession()->run();
    }
}
void Part::sendInput( const QString& text )
{
    Q_ASSERT( activeSession() );
    activeSession()->emulation()->sendText(text);
}

int Part::terminalProcessId()
{
    Q_ASSERT( activeSession() );

    return activeSession()->processId();

}

int Part::foregroundProcessId()
{
    Q_ASSERT( activeSession() );

    if (activeSession()->isForegroundProcessActive()) {
        return activeSession()->foregroundProcessId();
    } else {
        return -1;
    }
}

QString Part::foregroundProcessName()
{
    Q_ASSERT( activeSession() );

    if (activeSession()->isForegroundProcessActive()) {
        return activeSession()->foregroundProcessName();
    } else {
        return "";
    }
}

Session* Part::createSession(const Profile::Ptr profile)
{
    Session* session = SessionManager::instance()->createSession(profile);
    _viewManager->createView(session);

    return session;
}
void Part::activeViewChanged(SessionController* controller)
{
    Q_ASSERT( controller );
    Q_ASSERT( controller->view() );

    // remove existing controller
    if (_pluggedController)
    {

        disconnect(
          _pluggedController->session()->emulation(), SIGNAL(receiveLine(const QString&)),
          this, SIGNAL(receiveLine(const QString&))
        );

        removeChildClient (_pluggedController);
        disconnect(_pluggedController,SIGNAL(titleChanged(ViewProperties*)),this,
                    SLOT(activeViewTitleChanged(ViewProperties*)));
    }

    // insert new controller
    setupActionsForSession(controller);
    insertChildClient(controller);
    connect(controller,SIGNAL(titleChanged(ViewProperties*)),this,
            SLOT(activeViewTitleChanged(ViewProperties*)));
    activeViewTitleChanged(controller);

    const char* displaySignal = SIGNAL(overrideShortcutCheck(QKeyEvent*,bool&));
    const char* partSlot = SLOT(overrideTerminalShortcut(QKeyEvent*,bool&));

    disconnect(controller->view(),displaySignal,this,partSlot);
    connect(controller->view(),displaySignal,this,partSlot);

    _pluggedController = controller;
}
void Part::overrideTerminalShortcut(QKeyEvent* event, bool& override)
{
    // Shift+Insert is commonly used as the alternate shorcut for
    // pasting in KDE apps(including konsole), so it deserves some
    // special treatment.
    if ( (event->modifiers() & Qt::ShiftModifier) &&
         (event->key() == Qt::Key_Insert) )
    {
        override = false;
        return;
    }

    // override all shortcuts in the embedded terminal by default
    override = true;
    emit overrideShortcut(event,override);
}
void Part::activeViewTitleChanged(ViewProperties* properties)
{
    emit setWindowCaption(properties->title());
}
void Part::showManageProfilesDialog()
{
    showManageProfilesDialog(_viewManager->widget());
}
void Part::showManageProfilesDialog(QWidget* parent)
{
    ManageProfilesDialog* dialog = new ManageProfilesDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setShortcutEditorVisible(false);
    dialog->show();
}
void Part::showEditCurrentProfileDialog(QWidget* parent)
{
    Q_ASSERT( activeSession() );

    EditProfileDialog* dialog = new EditProfileDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProfile( SessionManager::instance()->sessionProfile(activeSession()) );
    dialog->show();
}
void Part::changeSessionSettings(const QString& text)
{
    // send a profile change command, the escape code format
    // is the same as the normal X-Term commands used to change the window title or icon,
    // but with a magic value of '50' for the parameter which specifies what to change
    Q_ASSERT( activeSession() );
    QByteArray buffer;
    buffer.append("\033]50;").append(text.toUtf8()).append('\a');

    activeSession()->emulation()->receiveData(buffer.constData(),buffer.length());
}

// Konqueror integration
bool Part::openUrl( const KUrl & _url )
{
    if ( url() == _url ) {
        emit completed();
        return true;
    }

    setUrl( _url );
    emit setWindowCaption( _url.pathOrUrl() );
    //kdDebug() << "Set Window Caption to " << url.pathOrUrl();
    emit started( 0 );

    if ( _url.isLocalFile() /*&& b_openUrls*/ ) {
        KDE_struct_stat buff;
        KDE::stat( QFile::encodeName( _url.path() ), &buff );
        QString text = ( S_ISDIR( buff.st_mode ) ? _url.path() : _url.directory() );
        showShellInDir( text );
    } else {
        showShellInDir( QDir::homePath() );
    }

    emit completed();
    return true;
}

void Part::setMonitorSilenceEnabled(bool enabled)
{
    Q_ASSERT( activeSession() );

    if (enabled)
    {
        activeSession()->setMonitorSilence(true);
        connect(activeSession(), SIGNAL(stateChanged(int)), this, SLOT(sessionStateChanged(int)), Qt::UniqueConnection);
    }
    else
    {
        activeSession()->setMonitorSilence(false);
        disconnect(activeSession(), SIGNAL(stateChanged(int)), this, SLOT(sessionStateChanged(int)));
    }
}

void Part::setMonitorActivityEnabled(bool enabled)
{
    Q_ASSERT( activeSession() );

    if (enabled)
    {
        activeSession()->setMonitorActivity(true);
        connect(activeSession(), SIGNAL(stateChanged(int)), this, SLOT(sessionStateChanged(int)), Qt::UniqueConnection);
    }
    else
    {
        activeSession()->setMonitorActivity(false);
        disconnect(activeSession(), SIGNAL(stateChanged(int)), this, SLOT(sessionStateChanged(int)));
    }
}

void Part::sessionStateChanged(int state)
{
    if (state == NOTIFYSILENCE)
        emit silenceDetected();
    else if (state == NOTIFYACTIVITY)
        emit activityDetected();
}

#include "Part.moc"
