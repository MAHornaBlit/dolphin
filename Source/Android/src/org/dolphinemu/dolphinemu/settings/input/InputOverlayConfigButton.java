/**
 * Copyright 2013 Dolphin Emulator Project
 * Licensed under GPLv2
 * Refer to the license.txt file included.
 */

package org.dolphinemu.dolphinemu.settings.input;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.View.OnTouchListener;
import android.widget.Button;

/**
 * A movable {@link Button} for use within the
 * input overlay configuration screen.
 */
public final class InputOverlayConfigButton extends Button implements OnTouchListener
{
	// SharedPreferences instance that the button positions are cached to.
	private final SharedPreferences sharedPrefs;

	/**
	 * Constructor
	 * 
	 * @param context The current {@link Context}.
	 * @param attribs {@link AttributeSet} for parsing XML attributes.
	 */
	public InputOverlayConfigButton(Context context, AttributeSet attribs)
	{
		super(context, attribs);

		// Set the button as its own OnTouchListener.
		setOnTouchListener(this);

		// Get the SharedPreferences instance.
		sharedPrefs = PreferenceManager.getDefaultSharedPreferences(context);
		
		// String ID of this button.
		final String buttonId = getResources().getResourceEntryName(getId());

		// Check if this button has previous values set that aren't the default.
		final float x = sharedPrefs.getFloat(buttonId+"-X", -1f);
		final float y = sharedPrefs.getFloat(buttonId+"-Y", -1f);

		// If they are not -1, then they have a previous value set.
		// Thus, we set those coordinate values.
		// TODO: This is not always correct placement. Fix this.
		// Likely something to do with the backing layout being a relative layout.
		if (x != -1f && y != -1f)
		{
			setX(x);
			setY(y);
		}
	}

	@Override
	public boolean onTouch(View v, MotionEvent event)
	{
		switch(event.getAction())
		{
			// Only change the X/Y coordinates when we move the button.
			case MotionEvent.ACTION_MOVE:
			{
				setX(getX() + event.getX());
				setY(getY() + event.getY());
				return true;
			}

			// Whenever the press event has ended
			// is when we save all of the information.
			case MotionEvent.ACTION_UP:
			{
				// String ID of this button.
				String buttonId = getResources().getResourceEntryName(getId());

				// Add the current X and Y positions of this button into SharedPreferences.
				SharedPreferences.Editor editor = sharedPrefs.edit();
				editor.putFloat(buttonId+"-X", getX());
				editor.putFloat(buttonId+"-Y", getY());
				editor.commit();
			}
		}

		return false;
	}
}