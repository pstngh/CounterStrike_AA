#include "rkhud_loadingscreen.h"

#include "../../../../thirdparty/RmlUi/Include/RmlUi/Core.h"

#include "cbase.h"
#include "cdll_client_int.h" // extern globals to interfaces like engineclient
#include "rkhud_teammenu.h"
#include "c_cs_player.h"

Rml::ElementDocument *RocketLoadingScreenDocument::m_pInstance = nullptr;
bool RocketLoadingScreenDocument::m_bVisible = false;
bool RocketLoadingScreenDocument::m_bGrabbingInput = false;

/* Event Listener added to continue button */
class RkLoadingScreenClick : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event& keyevent) override
    {
        // On any mousedown
        RocketLoadingScreenDocument::ShowPanel( false );
    }
};

static RkLoadingScreenClick loadingScreenClickListener;

RocketLoadingScreenDocument::RocketLoadingScreenDocument()
{
}

void RocketLoadingScreenDocument::LoadDialog()
{
    if( !m_pInstance )
    {
        m_pInstance = RocketUI()->LoadDocumentFile( ROCKET_CONTEXT_HUD, "hud_loadingscreen.rml", RocketLoadingScreenDocument::LoadDialog, RocketLoadingScreenDocument::UnloadDialog );

        if( !m_pInstance )
        {
            Error( "Couldn't create rocketui loadingscreen!\n");
            /* Exit */
        }
#if defined( USE_MAC_PRESET )
        // The bundled layout contains placeholder copy and a Continue button.
        // Keep the loading backdrop, but do not paint either for the Mac preset.
        if( Rml::Element *center = m_pInstance->GetElementById( "center" ) )
            center->SetProperty( "display", "none" );
#else
        m_pInstance->AddEventListener( Rml::EventId::Mousedown, &loadingScreenClickListener );
#endif
    }
}

void RocketLoadingScreenDocument::UnloadDialog()
{
    if( m_pInstance )
    {
        m_pInstance->Close();
        m_pInstance = nullptr;
    }
    if( m_bGrabbingInput )
    {
        RocketUI()->DenyInputToGame( false, "LoadingScreen" );
    }
}

void RocketLoadingScreenDocument::ShowPanel(bool bShow, bool immediate)
{
    // oh yeah buddy this'll get called before the loading sometimes
    // so if it does, load it for the caller.
    if( bShow )
    {
        if( !m_pInstance )
            LoadDialog();

        m_pInstance->Show();

        if( !m_bGrabbingInput )
        {
            RocketUI()->DenyInputToGame( true, "LoadingScreen" );
            m_bGrabbingInput = true;
        }
    }
    else
    {
        if( m_pInstance ){
            //UnloadDialog();
            m_pInstance->Hide();
        }

        if( m_bGrabbingInput )
        {
            RocketUI()->DenyInputToGame( false, "LoadingScreen" );
            m_bGrabbingInput = false;
        }

        // if we were visible, we need to join the game most likely and show the team select.
        if( m_bVisible )
        {
            engine->ClientCmd_Unrestricted( "joingame" );
#if defined( USE_MAC_PRESET )
            // Local launcher games assign the player to CT automatically. Do not
            // put a redundant team-selection screen in front of the spawned player.
            if( engine->IsClientLocalToActiveServer() )
                RocketTeamMenuDocument::ShowPanel( false );
            else
                RocketTeamMenuDocument::ShowPanel( true );
#else
            RocketTeamMenuDocument::ShowPanel( true );
#endif
        }
    }

    m_bVisible = bShow;
}
