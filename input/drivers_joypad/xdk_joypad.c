/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

#include "../input_driver.h"
#include "../../tasks/tasks_internal.h"

typedef struct
{
   XINPUT_STATE xstate;
   bool connected;
} xinput_joypad_state;

static xinput_joypad_state g_xinput_states[MAX_PADS];

static uint64_t pad_state[MAX_PADS];
static int16_t analog_state[MAX_PADS][2][2];
#ifdef _XBOX1
static HANDLE gamepads[MAX_PADS];
#endif

static const char* const XBOX_CONTROLLER_NAMES[4] =
{
   "XInput Controller (User 1)",
   "XInput Controller (User 2)",
   "XInput Controller (User 3)",
   "XInput Controller (User 4)"
};

static const char *xdk_joypad_name(unsigned pad)
{
   return XBOX_CONTROLLER_NAMES[pad];
}

static void xdk_joypad_autodetect_add(unsigned autoconf_pad)
{
   if (!input_autoconfigure_connect(
         xdk_joypad_name(autoconf_pad),
         NULL,
         xdk_joypad.ident,
         autoconf_pad,
         0,
         0))
      input_config_set_device_name(autoconf_pad, xdk_joypad_name(autoconf_pad));
}

static bool xdk_joypad_init(void *data)
{
#ifdef _XBOX1
   XInitDevices(0, NULL);
#else
   unsigned autoconf_pad;
   for (autoconf_pad = 0; autoconf_pad < MAX_USERS; autoconf_pad++)
      xdk_joypad_autodetect_add(autoconf_pad);
#endif

   (void)data;

   return true;
}

static bool xdk_joypad_button(unsigned port_num, uint16_t joykey)
{
   if (port_num >= MAX_PADS)
      return false;

   return pad_state[port_num] & (UINT64_C(1) << joykey);
}

static void xdk_joypad_get_buttons(unsigned port_num, retro_bits_t *state)
{
	if (port_num < MAX_PADS)
   {
		BITS_COPY16_PTR( state, pad_state[port_num] );
	}
   else
      BIT256_CLEAR_ALL_PTR(state);
}

static int16_t xdk_joypad_axis(unsigned port_num, uint32_t joyaxis)
{
   int val     = 0;
   int axis    = -1;
   bool is_neg = false;
   bool is_pos = false;

   if (joyaxis == AXIS_NONE || port_num >= MAX_PADS)
      return 0;

   if (AXIS_NEG_GET(joyaxis) < 4)
   {
      axis = AXIS_NEG_GET(joyaxis);
      is_neg = true;
   }
   else if (AXIS_POS_GET(joyaxis) < 4)
   {
      axis = AXIS_POS_GET(joyaxis);
      is_pos = true;
   }

   switch (axis)
   {
      case 0:
         val = analog_state[port_num][0][0];
         break;
      case 1:
         val = analog_state[port_num][0][1];
         break;
      case 2:
         val = analog_state[port_num][1][0];
         break;
      case 3:
         val = analog_state[port_num][1][1];
         break;
   }

   if (is_neg && val > 0)
      val = 0;
   else if (is_pos && val < 0)
      val = 0;

   return val;
}

static void xdk_joypad_poll(void)
{
   unsigned port;
#if defined(_XBOX1)
   DWORD dwInsertions, dwRemovals;

#ifdef __cplusplus
   XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD,
         reinterpret_cast<PDWORD>(&dwInsertions),
         reinterpret_cast<PDWORD>(&dwRemovals));
#else
   XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD,
         (PDWORD)&dwInsertions,
         (PDWORD)&dwRemovals);
#endif
#endif

   for (port = 0; port < MAX_PADS; port++)
   {
      unsigned i, j;
      uint64_t *state_cur    = NULL;
#if defined(_XBOX1)
      bool device_removed    = false;
      bool device_inserted   = false;

      /* handle inserted devices. */
      /* handle removed devices. */
      if (dwRemovals & (1 << port))
         device_removed = true;
      if (dwInsertions & (1 << port))
         device_inserted = true;

      if (device_removed)
      {
         /* if the controller was removed after
          * XGetDeviceChanges but before
          * XInputOpen, the device handle will be NULL. */
         if (gamepads[port])
            XInputClose(gamepads[port]);

         gamepads[port]  = 0;
         pad_state[port] = 0;

         input_autoconfigure_disconnect(port, xdk_joypad.ident);
      }

      if (device_inserted)
      {
         XINPUT_POLLING_PARAMETERS m_pollingParameters;

         m_pollingParameters.fAutoPoll       = FALSE;
         m_pollingParameters.fInterruptOut   = TRUE;
         m_pollingParameters.bInputInterval  = 8;
         m_pollingParameters.bOutputInterval = 8;

         gamepads[port]                      = XInputOpen(
               XDEVICE_TYPE_GAMEPAD, port,
               XDEVICE_NO_SLOT, &m_pollingParameters);

         xdk_joypad_autodetect_add(port);
      }

      if (!gamepads[port])
         continue;

      /* if the controller is removed after
       * XGetDeviceChanges but before XInputOpen,
       * the device handle will be NULL. */
      if (XInputPoll(gamepads[port]) != ERROR_SUCCESS)
         continue;
#endif
      
      memset(&g_xinput_states[port], 0, sizeof(xinput_joypad_state));

      g_xinput_states[port].connected = !
      (XInputGetState(
#ifdef _XBOX
         gamepads[port]
#else
         port
#endif
         , &g_xinput_states[port].xstate) == ERROR_DEVICE_NOT_CONNECTED);

      if (!g_xinput_states[port].connected)
         continue;

      state_cur  = &pad_state[port];

      *state_cur = 0;
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_LEFT) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_RIGHT) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_UP) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_DOWN) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_START) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_START) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_SELECT) : 0);

#if defined(_XBOX1)
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_A) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_B) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_Y]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_X) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_X]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_Y) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_WHITE]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L2) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_BLACK]) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R2) : 0);
#elif defined(_XBOX360)
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_B) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_A) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_B) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_X) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_X) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_Y) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bLeftTrigger > 128) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.bRightTrigger > 128) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L2) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R2) : 0);
#endif
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_L3) : 0);
      *state_cur |= ((g_xinput_states[port].xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) ? (UINT64_C(1) << RETRO_DEVICE_ID_JOYPAD_R3) : 0);

      analog_state[port][RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_X]  = g_xinput_states[port].xstate.Gamepad.sThumbLX;
      analog_state[port][RETRO_DEVICE_INDEX_ANALOG_LEFT][RETRO_DEVICE_ID_ANALOG_Y]  = g_xinput_states[port].xstate.Gamepad.sThumbLY;
      analog_state[port][RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_X] = g_xinput_states[port].xstate.Gamepad.sThumbRX;
      analog_state[port][RETRO_DEVICE_INDEX_ANALOG_RIGHT][RETRO_DEVICE_ID_ANALOG_Y] = g_xinput_states[port].xstate.Gamepad.sThumbRY;

      for (i = 0; i < 2; i++)
         for (j = 0; j < 2; j++)
            if (analog_state[port][i][j] == -0x8000)
               analog_state[port][i][j] = -0x7fff;
   }
}

static bool xdk_joypad_query_pad(unsigned pad)
{
   return pad < MAX_USERS && g_xinput_states[pad].connected;
}

static void xdk_joypad_destroy(void)
{
   unsigned i;

   for (i = 0; i < MAX_PADS; i++)
   {
      pad_state[i] = 0;
      analog_state[i][0][0] = 0;
      analog_state[i][0][1] = 0;
      analog_state[i][1][0] = 0;
      analog_state[i][1][1] = 0;
#if defined(_XBOX1)
      if (gamepads[i])
         XInputClose(gamepads[i]);
      gamepads[i]  = 0;
#endif
   }
}

input_device_driver_t xdk_joypad = {
   xdk_joypad_init,
   xdk_joypad_query_pad,
   xdk_joypad_destroy,
   xdk_joypad_button,
   xdk_joypad_get_buttons,
   xdk_joypad_axis,
   xdk_joypad_poll,
   NULL,
   xdk_joypad_name,
   "xdk",
};
