package com.android.settings;

import com.android.settings.ShortcutListDlg.AppsListAdapter.AppItem;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ComponentName;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.BitmapDrawable;
import android.provider.Browser;
import android.util.Log;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.Adapter;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.AutoCompleteTextView;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public class ShortcutListDlg extends AlertDialog implements OnItemClickListener{    
    public class AppsListAdapter extends android.widget.BaseAdapter{
        class AppItem{
            Bitmap bitmap;
            String description;
            Intent intent;
            void recycle(){
                if(bitmap != null){
                    bitmap.recycle();
                }
            }
        };

        private ArrayList<AppItem> mItems = new ArrayList<AppItem>();
        private LayoutInflater mInflater;
        private PackageManager mPackageManager ;
        private Context mContext; 
        
        public AppsListAdapter(Context context){
            mInflater = LayoutInflater.from(context);
            mPackageManager = context.getPackageManager();
            mContext = context;
        }
        public void setAppItems(Context context , List<ResolveInfo> apps){
            recycle();
            AppItem item;
            for(ResolveInfo app:apps){
                item = new AppItem();
                item.description = app.loadLabel(mPackageManager).toString();
                item.bitmap = ShortcutKeyDialog.createIconBitmap(app.activityInfo.loadIcon(mPackageManager),context);
                ComponentName cn = new ComponentName(app.activityInfo.applicationInfo.packageName,
                        app.activityInfo.name);
                item.intent = new Intent();
                item.intent.setComponent(cn);
                mItems.add(item);
            }
        }
        
        @Override
        public int getCount() {
            return mItems.size()+2;
        }        

        @Override
        public Object getItem(int id) {
            return mItems.get(id);
        }

        @Override
        public long getItemId(int position) {
            return position - 2;
        }

        @Override
        public View getView(int position, View convert, ViewGroup parent) {
            if(position >= 2){
                if(convert == null || convert.findViewById(R.id.pick_app) == null){
                    convert = mInflater.inflate(R.layout.shortcut_item_app, parent ,false);
                }
                ImageView itemIcon = (ImageView)convert.findViewById(R.id.item_icon);
                TextView targetDescription = (TextView)convert.findViewById(R.id.item_description);
                AppItem item = mItems.get(position - 2);
                itemIcon.setImageBitmap(item.bitmap);
                targetDescription.setText(item.description);
            }else if(position == 0){
                if(convert == null || convert.findViewById(R.id.pick_website) == null){
                    convert = mInflater.inflate(R.layout.shortcut_item_pick_website, parent ,false);
                }
            }else if(position == 1){
                if(convert == null || convert.findViewById(R.id.input_website) == null){
                    convert = mInflater.inflate(R.layout.shortcut_item_input_website, parent ,false);
                }
            }
            return convert;
        }
        
        public void recycle(){
            for(AppItem item:mItems){
                item.recycle();
            }
            mItems.clear();
        }
    };
    
    public interface Callbacks {
        void setShortKey(int keycode, String str);
    };
    
    public class WebsiteList extends AlertDialog implements OnItemClickListener{
        
        class WebsiteAdapter extends BaseAdapter{
            ArrayList<String> listTitle;
            ArrayList<String> listUrl;
            ArrayList<Bitmap> listBitmap;
            private Context mContext;
            private LayoutInflater mInflater;
            
            @Override
            public int getCount() {
                return listTitle.size();
            }
            
            public void initWebList(Context context){
                recycle();
                mContext = context;
                mInflater = LayoutInflater.from(context);
                listTitle = new ArrayList<String>();
                listUrl = new ArrayList<String>();
                listBitmap = new ArrayList<Bitmap>();
                
                String orderBy = Browser.BookmarkColumns.VISITS + " DESC";
                String whereClause = Browser.BookmarkColumns.BOOKMARK + " = 1";
                ContentResolver contentResolver = context.getContentResolver();
                Cursor cursor = contentResolver.query(Browser.BOOKMARKS_URI, 
                        Browser.HISTORY_PROJECTION, whereClause, null, orderBy);
                
                while(cursor!=null && cursor.moveToNext()){
                    listTitle.add(cursor.getString(cursor.getColumnIndex(Browser.BookmarkColumns.TITLE)));
                    listUrl.add(cursor.getString(cursor.getColumnIndex(Browser.BookmarkColumns.URL)));
                    byte[] b = cursor.getBlob(cursor.getColumnIndex(Browser.BookmarkColumns.THUMBNAIL));
                    if(b!=null){
                        listBitmap.add(BitmapFactory.decodeByteArray(b, 0, b.length));
                    }else{
                        listBitmap.add(BitmapFactory.decodeResource(context.getResources(), 
                                R.drawable.browser_thumbnail));
                    }
                }
                cursor.close();
            }
            
            public void recycle(){
                if(listBitmap != null){
                    for(Bitmap bm:listBitmap){
                        if(bm !=null && !bm.isRecycled()){
                            bm.recycle();
                            bm = null;
                        }
                    }
                }
            }

            @Override
            public String getItem(int id) {
                return listUrl.get(id);
            }

            @Override
            public long getItemId(int position) {
                return position;
            }

            @Override
            public View getView(int position, View convert, ViewGroup parent) {
                if(convert == null){
                    convert = mInflater.inflate(R.layout.shortcut_website_list, parent ,false);
                }
                ImageView thumbnail = (ImageView) convert.findViewById(R.id.website_thumbnail);
                TextView title = (TextView)convert.findViewById(R.id.website_title);
                TextView url = (TextView)convert.findViewById(R.id.website_url);
                
                thumbnail.setImageBitmap(listBitmap.get(position));
                title.setText(listTitle.get(position));
                url.setText(listUrl.get(position));
                return convert;
            }
            
        };

        private WebsiteAdapter mAdapter;
        protected WebsiteList(Context context) {
            super(context); 
            setTitle(R.string.shortcut_website_list);
            ListView listView = new ListView(context);
            mAdapter = new WebsiteAdapter();
            mAdapter.initWebList(context);
            listView.setAdapter(mAdapter);
            listView.setOnItemClickListener(this);
            setView(listView);
        } 

        @Override
        public void dismiss() {            
            super.dismiss();
            mAdapter.recycle();
        }
        @Override
        public void onItemClick(AdapterView<?> parent, View child, int position, long id) {
            String url = (String) parent.getItemAtPosition(position);
            setWebInfo(mKeycode,url);
            dismiss();
        }        
    }
    
    public ListView mListView;
    public AppsListAdapter mListAdapter; 
    
    private Callbacks mCallback;
    private int mKeycode;
    private View mWebsiteInputField;

    public ShortcutListDlg(Context context) {
        super(context);
        setupView(context);
    }

    public ShortcutListDlg(Context context, int theme) {
        super(context, theme);        
    }

    public ShortcutListDlg(Context context, boolean cancelable, OnCancelListener cancelListener) {
        super(context, cancelable, cancelListener);        
    }

    

    @Override
    public void dismiss() {        
        super.dismiss();
        mListAdapter.recycle();
    }

    @Override
    public void show() {
        super.show();
    }
    
    public void setKeycodeAndCallback(int keycode , Callbacks callback){
        mKeycode = keycode;
        mCallback = callback;
    }

    private void setupView(final Context context){
        setTitle(R.string.shortcut_pick_title);
        
        LayoutInflater inflater = LayoutInflater.from(context);
        View contentView = inflater.inflate(R.layout.shortcut_item_list, null);
        mListView = (ListView) contentView.findViewById(R.id.apps_list);
        mListAdapter = new AppsListAdapter(context);
        List<ResolveInfo> apps = getAppsInfo(context);
        mListAdapter.setAppItems(context, apps);       
        mListView.setAdapter(mListAdapter);
        mListView.setOnItemClickListener(this);
        setView(contentView);
    }
    
    private List<ResolveInfo> getAppsInfo(Context context){
        final PackageManager packageManager = context.getPackageManager();
        List<ResolveInfo> apps = null;
        final Intent mainIntent = new Intent(Intent.ACTION_MAIN, null);
        mainIntent.addCategory(Intent.CATEGORY_LAUNCHER);
        apps = packageManager.queryIntentActivities(mainIntent, 0);
        return apps;
    }
    
    public ArrayAdapter<String> getWebsiteAutoComplete(Context context){
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(context, R.layout.auto_complete_field,R.id.text_field);
        String orderBy = Browser.BookmarkColumns.VISITS + " DESC";
        String whereClause = Browser.BookmarkColumns.BOOKMARK + " = 1";
        ContentResolver contentResolver = context.getContentResolver();
        Cursor cursor = contentResolver.query(Browser.BOOKMARKS_URI, 
                Browser.HISTORY_PROJECTION, whereClause, null, orderBy);
        while(cursor!=null && cursor.moveToNext()){
            String str = cursor.getString(cursor.getColumnIndex(Browser.BookmarkColumns.URL));
            adapter.add(str);
        }
        cursor.close();
        return adapter;
    }
    
    public void setWebInfo(int keyCode , String url){
        String SPLIT = android.provider.Settings.System.SHORTCUT_PATH_SEPARATOR;
        String values = String.format("website" + SPLIT + "%s",url);
        mCallback.setShortKey(mKeycode, values);
        dismiss();
    }
    
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event){
        if(mListView.getSelectedItemPosition() == 1 && 
                keyCode == KeyEvent.KEYCODE_DPAD_CENTER){
            mWebsiteInputField.requestFocus();
        }
        return super.onKeyDown(keyCode, event);        
    }

    @Override
    public void onItemClick(AdapterView<?> parent, View childs, int position, long id) {
        if(mCallback == null)
            return;
        Log.d("v","onItemClick");
        if(position >= 2 ){
            AppsListAdapter.AppItem item = (AppItem) mListAdapter.getItem((int)id);
            String values;
            String SPLIT = android.provider.Settings.System.SHORTCUT_PATH_SEPARATOR;
            values = String.format("app"+ SPLIT +"%s" + SPLIT + "%s", 
                    item.intent.getComponent().getPackageName(),
                    item.intent.getComponent().getClassName());
            mCallback.setShortKey(mKeycode, values);
            dismiss();
        }else if(position == 1){
            LayoutInflater inflater = LayoutInflater.from(getContext());
            final View view = inflater.inflate(R.layout.input_layout, null);
            final Dialog dialog = new Dialog(getContext());
            view.findViewById(R.id.input_create_b).setOnClickListener(new View.OnClickListener() {                
                @Override
                public void onClick(View arg0) {
                    EditText et = (EditText)view.findViewById(R.id.input_inputText);
                    String url = et.getText().toString();
                    if(!url.startsWith("http://") && !url.startsWith("http:/") && !url.startsWith("http:")){
                    	url = "http://" + url;
                    }else if(url.startsWith("http:/")){
                    	url = url.replace("http:/", "http://");
                    }else if(url.startsWith("http:")){
                    	url = url.replace("http:", "http://");
                    }
                    setWebInfo(mKeycode,url);
                    dialog.dismiss();
                }
            });
            view.findViewById(R.id.input_cancel_b).setOnClickListener(new View.OnClickListener() {                
                @Override
                public void onClick(View arg0) {
                    dialog.dismiss();
                }
            });
            dialog.setTitle(R.string.input_website_title);
            dialog.setContentView(view);
            dialog.show();
        }else if(position == 0){
            WebsiteList dialog = new WebsiteList(getContext());
            dialog.show();
        }
    }
}
