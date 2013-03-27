package com.allwinner.TvdVideo;

import android.content.Context;
import android.graphics.drawable.Drawable;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

public class IconifiedTextView extends LinearLayout
{
	//һ���ļ������ļ�����ͼ��
	//����һ����ֱ���Բ���
	private TextView	mText	= null;
	private ImageView	mIcon	= null;
	public IconifiedTextView(Context context, int id, String text) 
	{
		super(context);
		//���ò��ַ�ʽ
		View view;
		LayoutInflater inflate = (LayoutInflater) context.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
		view = inflate.inflate(R.layout.list_item, null);
		mIcon = (ImageView) view.findViewById(R.id.list_icon);
		mText = (TextView) view.findViewById(R.id.list_text);
		mIcon.setImageDrawable(context.getResources().getDrawable(id));
		mText.setText(text);
		mText.setTextColor(0xffffffff);
		addView(view);
		
		//this.setOrientation(HORIZONTAL);
		//mIcon = new ImageView(context);
		////����ImageViewΪ�ļ���ͼ��
		//mIcon.setImageDrawable(context.getResources().getDrawable(id));
		////����ͼ���ڸò����е����λ��
		//mIcon.setPadding(8, 6, 6, 6); 
		////��ImageView��ͼ����ӵ��ò�����
		//addView(mIcon,  new LinearLayout.LayoutParams(
		//		LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT));
		////�����ļ�������䷽ʽ�������С
		//mText = new TextView(context);
		//mText.setText(text);
		//mText.setPadding(8, 0, 6, 0); 
		//mText.setTextSize(20);
		//mText.setSingleLine(true);
		////���ļ�����ӵ�������
		//addView(mText, new LinearLayout.LayoutParams(
		//		LayoutParams.FILL_PARENT, LayoutParams.FILL_PARENT));
	}
	//�����ļ���
	public void setText(String words)
	{
		mText.setText(words);
	}
	//����ͼ��
	public void setIcon(Drawable bullet)
	{
		mIcon.setImageDrawable(bullet);
	}
}

