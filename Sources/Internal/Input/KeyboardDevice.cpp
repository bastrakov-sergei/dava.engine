/*==================================================================================
    Copyright (c) 2008, binaryzebra
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    * Neither the name of the binaryzebra nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE binaryzebra AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL binaryzebra BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
=====================================================================================*/


#include "KeyboardDevice.h"

namespace DAVA
{

KeyboardDevice::KeyboardDevice()
{
	ClearAllKeys();
    PrepareKeyTranslator();
}

KeyboardDevice::~KeyboardDevice()
{
}
    
bool KeyboardDevice::IsKeyPressed(int32 keyCode)
{
#ifdef __DAVAENGINE_WIN32__
	if(DVKEY_ALT == keyCode)
	{
		SHORT isAlt = GetAsyncKeyState(VK_MENU);
		return isAlt != 0;
	}
#endif 

    DVASSERT(keyCode < DVKEY_COUNT);
    return keyStatus[keyCode];
}

void KeyboardDevice::OnKeyPressed(int32 keyCode)
{
    DVASSERT(keyCode < DVKEY_COUNT);
    keyStatus[keyCode] = true;
    realKeyStatus[keyCode] = true;
}

void KeyboardDevice::OnKeyUnpressed(int32 keyCode)
{
    DVASSERT(keyCode < DVKEY_COUNT);
    realKeyStatus[keyCode] = false;
}

void KeyboardDevice::OnBeforeUpdate()
{
}

void KeyboardDevice::OnAfterUpdate()
{
    for (int32 i = 0; i < DVKEY_COUNT; i++) 
    {
        keyStatus[i] = realKeyStatus[i];
    }
}
    
int32 KeyboardDevice::GetDavaKeyForSystemKey(int32 systemKeyCode)
{
    DVASSERT(systemKeyCode < MAX_KEYS);
    return keyTranslator[systemKeyCode];
}

void KeyboardDevice::OnSystemKeyPressed(int32 systemKeyCode)
{
    DVASSERT(systemKeyCode < MAX_KEYS);
    OnKeyPressed(keyTranslator[systemKeyCode]);
}

void KeyboardDevice::OnSystemKeyUnpressed(int32 systemKeyCode)
{
    DVASSERT(systemKeyCode < MAX_KEYS);
    OnKeyUnpressed(keyTranslator[systemKeyCode]);
}

    
void KeyboardDevice::PrepareKeyTranslator()
{
    for (int i = 0; i < MAX_KEYS; i++) 
    {
        keyTranslator[i] = DVKEY_UNKNOWN;
    }
#if defined(__DAVAENGINE_WIN32__)
	keyTranslator[VK_LEFT] = DVKEY_LEFT;
	keyTranslator[VK_RIGHT] = DVKEY_RIGHT;
	keyTranslator[VK_UP] = DVKEY_UP;
	keyTranslator[VK_DOWN] = DVKEY_DOWN;
	keyTranslator[VK_ESCAPE] = DVKEY_ESCAPE;
	keyTranslator[VK_DELETE] = DVKEY_DELETE;
	keyTranslator[VK_BACK] = DVKEY_BACKSPACE;
	keyTranslator[VK_RETURN] = DVKEY_ENTER;
	keyTranslator[VK_CONTROL] = DVKEY_CTRL;
	keyTranslator[VK_MENU] = DVKEY_ALT;
	keyTranslator[VK_SHIFT] = DVKEY_SHIFT;
	keyTranslator[VK_CAPITAL] = DVKEY_CAPSLOCK;
    keyTranslator[VK_SPACE] = DVKEY_SPACE;

	keyTranslator[VK_F1] = DVKEY_F1;
    
    // alpha keys
    for(int32 i = 0; i < 26; ++i)
    {
        keyTranslator[0x41 + i] = DVKEY_A + i;
    }
    
    // numeric keys & keys at num pad
    for(int32 i = 0; i < 10; ++i)
    {
        keyTranslator[0x30 + i] = DVKEY_0 + i;
        keyTranslator[0x60 + i] = DVKEY_NUMPAD0 + i;
    }
    
#endif

#if defined(__DAVAENGINE_HTML5__)
    keyTranslator[0x08] = DVKEY_BACKSPACE;
    keyTranslator[0x09] = DVKEY_TAB;
    keyTranslator[0x1B] = DVKEY_ESCAPE;
    keyTranslator[0x20] = DVKEY_SPACE;
    keyTranslator[0x23] = DVKEY_END;
    keyTranslator[0x24] = DVKEY_HOME;
    keyTranslator[0x50] = DVKEY_LEFT;
    keyTranslator[0x52] = DVKEY_UP;
    keyTranslator[0x4F] = DVKEY_RIGHT;
    keyTranslator[0x51] = DVKEY_DOWN;
    keyTranslator[0x7F] = DVKEY_DELETE;
    keyTranslator[0x0D] = DVKEY_ENTER;
    

    keyTranslator[0x61] = DVKEY_A;
    keyTranslator[0x64] = DVKEY_D;
    keyTranslator[0x65] = DVKEY_E;
    keyTranslator[0x71] = DVKEY_Q;
    keyTranslator[0x72] = DVKEY_R;
    keyTranslator[0x73] = DVKEY_S;
    keyTranslator[0x74] = DVKEY_T;
    keyTranslator[0x75] = DVKEY_U;
    keyTranslator[0x76] = DVKEY_V;
    keyTranslator[0x77] = DVKEY_W;
#endif
    
#if defined(__DAVAENGINE_MACOS__)
    keyTranslator[0x7B] = DVKEY_LEFT;
    keyTranslator[0x7C] = DVKEY_RIGHT;
    keyTranslator[0x7E] = DVKEY_UP;
    keyTranslator[0x7D] = DVKEY_DOWN;
	keyTranslator[0x75] = DVKEY_DELETE;
    keyTranslator[0x35] = DVKEY_ESCAPE;
    keyTranslator[0x33] = DVKEY_BACKSPACE;
    keyTranslator[0x24] = DVKEY_ENTER;
    keyTranslator[DVMACOS_COMMAND] = DVKEY_CTRL;
    keyTranslator[DVMACOS_OPTION] = DVKEY_ALT;
    keyTranslator[DVMACOS_SHIFT] = DVKEY_SHIFT;
    keyTranslator[DVMACOS_CAPS_LOCK] = DVKEY_CAPSLOCK;
    keyTranslator[0x31] = DVKEY_SPACE;
    

    keyTranslator[0x00] = DVKEY_A;
    keyTranslator[0x0B] = DVKEY_B;
    keyTranslator[0x08] = DVKEY_C;
    keyTranslator[0x02] = DVKEY_D;
    keyTranslator[0x0E] = DVKEY_E;
    keyTranslator[0x03] = DVKEY_F;
    keyTranslator[0x05] = DVKEY_G;
    keyTranslator[0x04] = DVKEY_H;
    keyTranslator[0x22] = DVKEY_I;
    keyTranslator[0x26] = DVKEY_J;
    keyTranslator[0x28] = DVKEY_K;
    keyTranslator[0x25] = DVKEY_L;
    keyTranslator[0x2D] = DVKEY_M;
    keyTranslator[0x2E] = DVKEY_N;
    keyTranslator[0x1F] = DVKEY_O;
    keyTranslator[0x23] = DVKEY_P;
    keyTranslator[0x0C] = DVKEY_Q;
    keyTranslator[0x0F] = DVKEY_R;
    keyTranslator[0x01] = DVKEY_S;
    keyTranslator[0x11] = DVKEY_T;
    keyTranslator[0x20] = DVKEY_U;
    keyTranslator[0x09] = DVKEY_V;
    keyTranslator[0x0D] = DVKEY_W;
    keyTranslator[0x07] = DVKEY_X;
    keyTranslator[0x10] = DVKEY_Y;
    keyTranslator[0x06] = DVKEY_Z;


    keyTranslator[0x1D] = DVKEY_0;
    keyTranslator[0x12] = DVKEY_1;
    keyTranslator[0x13] = DVKEY_2;
    keyTranslator[0x14] = DVKEY_3;
    keyTranslator[0x15] = DVKEY_4;
    keyTranslator[0x17] = DVKEY_5;
    keyTranslator[0x16] = DVKEY_6;
    keyTranslator[0x1A] = DVKEY_7;
    keyTranslator[0x1C] = DVKEY_8;
    keyTranslator[0x19] = DVKEY_9;
    keyTranslator[0x7A] = DVKEY_F1;
    keyTranslator[0x1B] = DVKEY_MINUS;
    keyTranslator[0x18] = DVKEY_EQUALS;

    
    // numeric keys at numpad
    for(int32 i = 0; i < 10; ++i)
    {
        keyTranslator[0x52 + i] = DVKEY_0 + i;
    }
    
#endif

#if defined(__DAVAENGINE_ANDROID__)

    keyTranslator[0x60] = DVKEY_BUTTON_A;
    keyTranslator[0x61] = DVKEY_BUTTON_B;
    keyTranslator[0x62] = DVKEY_BUTTON_C;
    keyTranslator[0x63] = DVKEY_BUTTON_X;
    keyTranslator[0x64] = DVKEY_BUTTON_Y;
    keyTranslator[0x65] = DVKEY_BUTTON_Z;
    keyTranslator[0x66] = DVKEY_BUTTON_L1;
    keyTranslator[0x68] = DVKEY_BUTTON_L2;
    keyTranslator[0x67] = DVKEY_BUTTON_R1;
    keyTranslator[0x69] = DVKEY_BUTTON_R2;
    keyTranslator[0x15] = DVKEY_DPAD_LEFT;
    keyTranslator[0x16] = DVKEY_DPAD_RIGHT;
    keyTranslator[0x13] = DVKEY_DPAD_UP;
    keyTranslator[0x14] = DVKEY_DPAD_DOWN;
    keyTranslator[0x17] = DVKEY_DPAD_CENTER;
    keyTranslator[0x6e] = DVKEY_BUTTON_MODE;
    keyTranslator[0x6d] = DVKEY_BUTTON_SELECT;
    keyTranslator[0x6c] = DVKEY_BUTTON_START;
	keyTranslator[0x04] = DVKEY_BUTTON_BACK;

#endif
}

void KeyboardDevice::ClearAllKeys()
{
	for (int32 i = 0; i < DVKEY_COUNT; i++) 
	{
		keyStatus[i] = realKeyStatus[i] = false;
	}
}



};
