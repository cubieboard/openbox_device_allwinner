package com.android.settings;

import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.ColorMatrix;
import android.graphics.ColorMatrixColorFilter;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.TableMaskFilter;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.PaintDrawable;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.BaseAdapter;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public class ShortcutKeyDialog extends AlertDialog 
        implements OnItemClickListener,ShortcutListDlg.Callbacks{
    private final String TAG = "Settings.SetShortcut";
    
    public class ShortcutAdapter extends BaseAdapter{

        private int mListEvent[];
        private LayoutInflater mInflater;
        private Context mContext;
        private ArrayList<Bitmap> mBitmaps;
        
        public ShortcutAdapter(Context context){
            mInflater = LayoutInflater.from(context);
            mContext = context;
        }
        
        public void setEventList(int[] eventList){
            mListEvent = eventList;
            mBitmaps = new ArrayList<Bitmap>(eventList.length);
            for(int i=0;i<eventList.length;i++){
                mBitmaps.add(null);
            }
        }
        @Override
        public int getCount() {
            return mListEvent.length;
        }

        @Override
        public Integer getItem(int id) {
            return mListEvent[id%mListEvent.length];
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public View getView(int position, View convert, ViewGroup parent) {
            if(convert == null){
                convert = mInflater.inflate(R.layout.shortcut_key_item, parent ,false);
            }
            ImageView keyIcon = (ImageView)convert.findViewById(R.id.key_icon);
            ImageView targetIcon = (ImageView)convert.findViewById(R.id.target_icon);
            TextView targetDescription = (TextView)convert.findViewById(R.id.target_description);
            
            int keycode = mListEvent[position];
            switch(keycode){
                case KeyEvent.KEYCODE_PROG_RED:
                    keyIcon.setImageResource(R.drawable.shortcut_key_icon_red);
                    break;
                case KeyEvent.KEYCODE_PROG_GREEN:
                    keyIcon.setImageResource(R.drawable.shortcut_key_icon_green);
                    break;
                case KeyEvent.KEYCODE_PROG_YELLOW:
                    keyIcon.setImageResource(R.drawable.shortcut_key_icon_yellow);
                    break;
                case KeyEvent.KEYCODE_PROG_BLUE:
                    keyIcon.setImageResource(R.drawable.shortcut_key_icon_blue);
                    break;
            }
            String name = android.provider.Settings.System.findNameByKey(keycode);
            if(name == null){
                Log.w(TAG, "the key " + keycode + " has no shortcut.");
            }
            ContentResolver resolver = mContext.getContentResolver();
            String str = android.provider.Settings.System.getString(resolver, name);
            if(str == null){
                Log.w(TAG, "fail in getting " + name + " from SettingProvider");
            }
            Log.d(TAG, "shortcut path is " + str);
            String[] strArray = str.split(android.provider.Settings.System.SHORTCUT_PATH_SEPARATOR);
            if(strArray == null){
                Log.w(TAG, "fail in spliting string " + str + " with substring \"--split--\".");
            }
            if(strArray[0].contentEquals("website") && strArray.length == 2 ){
                targetIcon.setImageResource(R.drawable.shortcut_key_website_icon);
                targetDescription.setText(strArray[1]);
            }else if(strArray[0].contentEquals("app") && strArray.length == 3){
                PackageManager packageManager = mContext.getPackageManager();
                Intent intent = new Intent();
                intent.setClassName(strArray[1], strArray[2]);
                List<ResolveInfo> appInfo = packageManager.queryIntentActivities(intent, 0);
                if(appInfo.size() > 0){
                    targetDescription.setText(appInfo.get(0).loadLabel(packageManager).toString());
                    Bitmap bm = mBitmaps.get(position);
                    if(bm != null){
                        bm.recycle();
                        bm = null;
                    }
                    bm = createIconBitmap(appInfo.get(0).activityInfo.loadIcon(packageManager),mContext);
                    targetIcon.setImageBitmap(bm);
                }
            }
            return convert;
        }
        public void recycle(){
            if(mBitmaps != null){
                for(Bitmap bm:mBitmaps){
                    if(bm != null){
                        bm.recycle();
                    }
                }
            }
        }
    };
    
    protected ListView mListView;
    protected ShortcutAdapter mAdapter;
    protected int [] mListEvent = new int[]{
            KeyEvent.KEYCODE_PROG_RED,
            KeyEvent.KEYCODE_PROG_GREEN,
            KeyEvent.KEYCODE_PROG_YELLOW,
            KeyEvent.KEYCODE_PROG_BLUE,};
    
    
    private static int sIconWidth = -1;
    private static int sIconHeight = -1;
    private static int sIconTextureWidth = -1;
    private static int sIconTextureHeight = -1;

    private static final Paint sPaint = new Paint();
    private static final Paint sBlurPaint = new Paint();
    private static final Paint sGlowColorPressedPaint = new Paint();
    private static final Paint sGlowColorFocusedPaint = new Paint();
    private static final Paint sDisabledPaint = new Paint();
    private static final Rect sBounds = new Rect();
    private static final Rect sOldBounds = new Rect();
    private static final Canvas sCanvas = new Canvas();
    static int sColors[] = { 0xffff0000, 0xff00ff00, 0xff0000ff };
    static int sColorIndex = 0;
    static public Bitmap createIconBitmap(Drawable icon, Context context) {
        synchronized (sCanvas) {
            if (sIconWidth == -1) {
                initStatics(context);
            }

            int width = sIconWidth;
            int height = sIconHeight;

            if (icon instanceof PaintDrawable) {
                PaintDrawable painter = (PaintDrawable) icon;
                painter.setIntrinsicWidth(width);
                painter.setIntrinsicHeight(height);
            } else if (icon instanceof BitmapDrawable) {
                // Ensure the bitmap has a density.
                BitmapDrawable bitmapDrawable = (BitmapDrawable) icon;
                Bitmap bitmap = bitmapDrawable.getBitmap();
                if (bitmap.getDensity() == Bitmap.DENSITY_NONE) {
                    bitmapDrawable.setTargetDensity(context.getResources().getDisplayMetrics());
                }
            }
            int sourceWidth = icon.getIntrinsicWidth();
            int sourceHeight = icon.getIntrinsicHeight();

            if (sourceWidth > 0 && sourceWidth > 0) {
                // There are intrinsic sizes.
                if (width < sourceWidth || height < sourceHeight) {
                    // It's too big, scale it down.
                    final float ratio = (float) sourceWidth / sourceHeight;
                    if (sourceWidth > sourceHeight) {
                        height = (int) (width / ratio);
                    } else if (sourceHeight > sourceWidth) {
                        width = (int) (height * ratio);
                    }
                } else if (sourceWidth < width && sourceHeight < height) {
                    // It's small, use the size they gave us.
                    width = sourceWidth;
                    height = sourceHeight;
                }
            }

            // no intrinsic size --> use default size
            int textureWidth = sIconTextureWidth;
            int textureHeight = sIconTextureHeight;

            final Bitmap bitmap = Bitmap.createBitmap(textureWidth, textureHeight,
                    Bitmap.Config.ARGB_8888);
            final Canvas canvas = sCanvas;
            canvas.setBitmap(bitmap);

            final int left = (textureWidth-width) / 2;
            final int top = (textureHeight-height) / 2;

            if (false) {
                // draw a big box for the icon for debugging
                canvas.drawColor(sColors[sColorIndex]);
                if (++sColorIndex >= sColors.length) sColorIndex = 0;
                Paint debugPaint = new Paint();
                debugPaint.setColor(0xffcccc00);
                canvas.drawRect(left, top, left+width, top+height, debugPaint);
            }

            sOldBounds.set(icon.getBounds());
            icon.setBounds(left, top, left+width, top+height);
            icon.draw(canvas);
            icon.setBounds(sOldBounds);

            return bitmap;
        }
    }
    
    private static void initStatics(Context context) {
        final Resources resources = context.getResources();
        final DisplayMetrics metrics = resources.getDisplayMetrics();
        final float density = metrics.density;

        sIconWidth = sIconHeight = (int) resources.getDimension(android.R.dimen.app_icon_size);
        sIconTextureWidth = sIconTextureHeight = sIconWidth + 2;

        sBlurPaint.setMaskFilter(new BlurMaskFilter(5 * density, BlurMaskFilter.Blur.NORMAL));
        sGlowColorPressedPaint.setColor(0xffffc300);
        sGlowColorPressedPaint.setMaskFilter(TableMaskFilter.CreateClipTable(0, 30));
        sGlowColorFocusedPaint.setColor(0xffff8e00);
        sGlowColorFocusedPaint.setMaskFilter(TableMaskFilter.CreateClipTable(0, 30));

        ColorMatrix cm = new ColorMatrix();
        cm.setSaturation(0.2f);
        sDisabledPaint.setColorFilter(new ColorMatrixColorFilter(cm));
        sDisabledPaint.setAlpha(0x88);
    }

    public ShortcutKeyDialog(Context context) {
        super(context);
        setupView(context);
    }
    
    public ShortcutKeyDialog(Context context, boolean cancelable, OnCancelListener cancelListener) {
        super(context, cancelable, cancelListener);
        setupView(context);
    }

    private void setupView(Context context){
        setTitle(R.string.shortcut_key_list_title);
        mListView = new ListView(context);
        mAdapter = new ShortcutAdapter(context);
        mAdapter.setEventList(mListEvent);
        mListView.setAdapter(mAdapter);
        mListView.setOnItemClickListener(this);
        setView(mListView);
    }
    
    

    @Override
    public void dismiss() {
        mAdapter.recycle();
        super.dismiss();
    }

    @Override
    public void onItemClick(AdapterView<?> parent, View child, int position, long id) {
        ShortcutListDlg dlg = new ShortcutListDlg(getContext());
        dlg.setKeycodeAndCallback(mAdapter.getItem((int) id), this);
        dlg.show();
    }

    @Override
    public void setShortKey(int keyCode, String values) {
        String name = android.provider.Settings.System.findNameByKey(keyCode);
        if(name == null){
            Log.w(TAG, "the key " + keyCode + " has no shortcut.");
            return;
        }
        String[] strArray = values.split(android.provider.Settings.System.SHORTCUT_PATH_SEPARATOR);
        if((strArray[0].contentEquals("website") && strArray.length < 3 ) ||
                (strArray[0].contentEquals("app") && strArray.length ==3 )){
            ContentResolver resolver = getContext().getContentResolver();
            android.provider.Settings.System.putString(resolver, name, values);
        }else{
            Log.w(TAG,"the target values is invalid!");
        }
        mAdapter.notifyDataSetChanged();
    }
}
