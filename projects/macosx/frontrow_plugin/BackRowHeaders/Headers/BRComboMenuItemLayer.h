/*
 *     Generated by class-dump 3.1.1.
 *
 *     class-dump is Copyright (C) 1997-1998, 2000-2001, 2004-2006 by Steve Nygard.
 */

#import <BackRow/BRLayer.h>

#import "BRMenuItemLayerProtocol.h"

@class BRAutoScrollingTextLayer, BRImageLayer, BRTextLayer, NSString;

@interface BRComboMenuItemLayer : BRLayer <BRMenuItemLayer>
{
    BRImageLayer *_thumbnailLayer;
    BRAutoScrollingTextLayer *_titleLayer;
    BRTextLayer *_subtitleLayer;
    BRTextLayer *_leftTextLayer;
    BRTextLayer *_middleTextLayer;
    BRImageLayer *_bottomRightImageLayer;
    BRImageLayer *_topRightImageLayer;
    NSString *_thumbnailIdentifier;
}

- (id)init;
- (void)dealloc;
- (float)defaultRowHeight;
- (void)setTitle:(id)fp8;
- (id)title;
- (void)setSubtitle:(id)fp8;
- (id)subtitle;
- (void)setLeftText:(id)fp8;
- (void)setMiddleText:(id)fp8;
- (void)setThumbnailImage:(id)fp8;
- (void)setThumbnailIdentifier:(id)fp8;
- (id)thumbnailIdentifier;
- (void)setBottomRightImage:(id)fp8;
- (void)setTopRightImage:(id)fp8;
- (void)highlight;
- (void)unHighlight;
- (void)scrollItemIfNecessary;
- (void)stopScrollingItem;
- (struct CGRect)frameForCellBounds:(struct CGSize)fp8;
- (void)layoutSublayers;
- (id)axItemText;

@end

