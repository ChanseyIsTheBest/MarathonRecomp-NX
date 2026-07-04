#include "exports.h"
#include <apu/embedded_player.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <app.h>

void Game_PlaySound(const char* pName)
{
     if (EmbeddedPlayer::s_isActive)
     {
         EmbeddedPlayer::Play(pName);
     }
     else
     {
         Game_PlaySound("system", pName);
     }
}

void Game_PlaySound(const char* pBankName, const char* pName)
{
    // The guest sound player is only reachable once the game is up; the
    // installer may end up here when the embedded player is unavailable.
    if (!App::s_isInit || App::s_pApp == nullptr || g_memory.base == nullptr || GetPPCContext() == nullptr)
        return;

    auto pBankNameGuest = g_userHeap.Alloc(strlen(pBankName) + 1);
    auto pNameGuest = g_userHeap.Alloc(strlen(pName) + 1);

    if (pBankNameGuest == nullptr || pNameGuest == nullptr)
    {
        g_userHeap.Free(pBankNameGuest);
        g_userHeap.Free(pNameGuest);
        return;
    }

    strcpy((char*)pBankNameGuest, pBankName);
    strcpy((char*)pNameGuest, pName);

    GuestToHostFunction<int>(sub_824C7868, App::s_pApp->m_pDoc->m_pRootTask.get(), pBankNameGuest, pNameGuest);

    g_userHeap.Free(pBankNameGuest);
    g_userHeap.Free(pNameGuest);
}
