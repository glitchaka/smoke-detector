struct AuthorPoseKey {
    int frame = 0;
    promptemon::bch::Vec3 translation{};
    promptemon::bch::Quat rotation{};
    promptemon::bch::Vec3 scale{1,1,1};
};

struct AuthorBoneKeys {
    std::vector<AuthorPoseKey> keys;
};

std::unordered_map<std::string, std::unordered_map<std::string, AuthorBoneKeys>> gAuthorKeys;

std::string authorClipKey(const promptemon::bch::Animation& animation) {
    return gSelectedModel + "::" + animation.name;
}

promptemon::bch::Vec3 authorLerp(promptemon::bch::Vec3 a, promptemon::bch::Vec3 b, float t) {
    return {a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

promptemon::bch::Quat authorNormalize(promptemon::bch::Quat q) {
    const float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len <= 1e-8f) return {0,0,0,1};
    q.x/=len; q.y/=len; q.z/=len; q.w/=len;
    return q;
}

promptemon::bch::Quat authorQuatFromEuler(promptemon::bch::Vec3 r) {
    const float cx=std::cos(r.x*.5f), sx=std::sin(r.x*.5f);
    const float cy=std::cos(r.y*.5f), sy=std::sin(r.y*.5f);
    const float cz=std::cos(r.z*.5f), sz=std::sin(r.z*.5f);
    return authorNormalize({
        sx*cy*cz-cx*sy*sz,
        cx*sy*cz+sx*cy*sz,
        cx*cy*sz-sx*sy*cz,
        cx*cy*cz+sx*sy*sz
    });
}

promptemon::bch::Vec3 authorEulerFromQuat(promptemon::bch::Quat q) {
    q=authorNormalize(q);
    const float sinr=2.0f*(q.w*q.x+q.y*q.z);
    const float cosr=1.0f-2.0f*(q.x*q.x+q.y*q.y);
    const float rx=std::atan2(sinr,cosr);
    const float sinp=2.0f*(q.w*q.y-q.z*q.x);
    const float ry=std::fabs(sinp)>=1.0f ? std::copysign(PI_F*.5f,sinp) : std::asin(sinp);
    const float siny=2.0f*(q.w*q.z+q.x*q.y);
    const float cosy=1.0f-2.0f*(q.y*q.y+q.z*q.z);
    const float rz=std::atan2(siny,cosy);
    return {rx,ry,rz};
}

promptemon::bch::Quat authorSlerp(promptemon::bch::Quat a, promptemon::bch::Quat b, float t) {
    a=authorNormalize(a); b=authorNormalize(b);
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0){dot=-dot;b={-b.x,-b.y,-b.z,-b.w};}
    if(dot>.9995f){return authorNormalize({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});}
    const float theta=std::acos(std::clamp(dot,-1.0f,1.0f));
    const float s=std::sin(theta);
    const float wa=std::sin((1.0f-t)*theta)/s;
    const float wb=std::sin(t*theta)/s;
    return {a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb,a.w*wa+b.w*wb};
}

template<typename T, typename Interpolate>
T authorSampleVector(const std::vector<T>& samples,float start,float end,float frame,const T& fallback,Interpolate interpolate) {
    if(samples.empty()) return fallback;
    if(samples.size()==1 || end<=start) return samples.front();
    const float u=std::clamp((frame-start)/(end-start),0.0f,1.0f)*(samples.size()-1);
    const std::size_t a=std::min<std::size_t>(static_cast<std::size_t>(u),samples.size()-1);
    const std::size_t b=std::min(a+1,samples.size()-1);
    return interpolate(samples[a],samples[b],u-static_cast<float>(a));
}

promptemon::bch::Vec3 authorSampleVec(const promptemon::bch::Vec3Track& track,float frame,promptemon::bch::Vec3 fallback) {
    if(!track.present) return fallback;
    if(track.constant) return track.value;
    return authorSampleVector(track.samples,track.start,track.end,frame,fallback,
        [](auto a,auto b,float t){return authorLerp(a,b,t);});
}

promptemon::bch::Quat authorSampleQuat(const promptemon::bch::QuatTrack& track,float frame,promptemon::bch::Quat fallback) {
    if(!track.present) return fallback;
    if(track.constant) return track.value;
    return authorSampleVector(track.samples,track.start,track.end,frame,fallback,
        [](auto a,auto b,float t){return authorSlerp(a,b,t);});
}

struct AuthorPose {
    promptemon::bch::Vec3 translation{};
    promptemon::bch::Quat rotation{};
    promptemon::bch::Vec3 scale{1,1,1};
};

const promptemon::bch::BoneTrack* authorFindTrack(const promptemon::bch::Animation& animation,const std::string& boneName) {
    const auto it=std::find_if(animation.tracks.begin(),animation.tracks.end(),[&](const auto&t){return t.boneName==boneName;});
    return it==animation.tracks.end()?nullptr:&*it;
}

promptemon::bch::BoneTrack& authorEnsureTrack(promptemon::bch::Animation& animation,const std::string& boneName) {
    auto it=std::find_if(animation.tracks.begin(),animation.tracks.end(),[&](const auto&t){return t.boneName==boneName;});
    if(it!=animation.tracks.end()) return *it;
    promptemon::bch::BoneTrack track; track.boneName=boneName; animation.tracks.push_back(std::move(track));
    return animation.tracks.back();
}

AuthorPose authorPoseAt(const promptemon::bch::Animation& animation,const promptemon::bch::Bone& bone,float frame) {
    AuthorPose pose;
    pose.translation=bone.bindTranslation;
    pose.rotation=authorQuatFromEuler(bone.bindRotation);
    pose.scale=bone.bindScale;
    if(const auto*track=authorFindTrack(animation,bone.name)){
        pose.translation=authorSampleVec(track->translation,frame,pose.translation);
        pose.rotation=authorSampleQuat(track->rotation,frame,pose.rotation);
        pose.scale=authorSampleVec(track->scale,frame,pose.scale);
    }
    return pose;
}

void authorSortKeys(AuthorBoneKeys& state) {
    std::sort(state.keys.begin(),state.keys.end(),[](const auto&a,const auto&b){return a.frame<b.frame;});
}

void authorPutKey(AuthorBoneKeys& state,const AuthorPoseKey& key) {
    auto it=std::find_if(state.keys.begin(),state.keys.end(),[&](const auto&k){return k.frame==key.frame;});
    if(it!=state.keys.end()) *it=key; else state.keys.push_back(key);
    authorSortKeys(state);
}

AuthorPose authorInterpolateKeys(const AuthorBoneKeys& state,int frame) {
    if(state.keys.empty()) return {};
    if(frame<=state.keys.front().frame){const auto&k=state.keys.front();return {k.translation,k.rotation,k.scale};}
    if(frame>=state.keys.back().frame){const auto&k=state.keys.back();return {k.translation,k.rotation,k.scale};}
    for(std::size_t i=1;i<state.keys.size();++i){
        if(frame>state.keys[i].frame) continue;
        const auto&a=state.keys[i-1];const auto&b=state.keys[i];
        const float t=static_cast<float>(frame-a.frame)/std::max(1,b.frame-a.frame);
        return {authorLerp(a.translation,b.translation,t),authorSlerp(a.rotation,b.rotation,t),authorLerp(a.scale,b.scale,t)};
    }
    const auto&k=state.keys.back();return {k.translation,k.rotation,k.scale};
}

std::size_t authorCurrentAnimationIndex() {
    if(!gScene||!gCurrent) return static_cast<std::size_t>(-1);
    const auto& animations=gScene->animations();
    for(std::size_t i=0;i<animations.size();++i) if(&animations[i]==gCurrent) return i;
    return static_cast<std::size_t>(-1);
}

promptemon::bch::Animation* authorEnsureAnimation() {
    if(!gScene) return nullptr;
    std::size_t index=authorCurrentAnimationIndex();
    if(index!=static_cast<std::size_t>(-1)) return &gScene->animationsMutable()[index];
    auto& animations=gScene->animationsMutable();
    promptemon::bch::Animation clip;
    clip.name="New_clip_"+std::to_string(animations.size()+1);
    clip.loop=true; clip.frames=30.0f;
    animations.push_back(std::move(clip));
    gIdle=nullptr;
    gCurrent=&animations.back();
    gFrame=0; gAnimationPaused=true;
    gAnimationScroll=animations.size()>6?animations.size()-6:0;
    setStatus("Created animation: "+gCurrent->name);
    return &animations.back();
}

void authorCreateAnimation() {
    if(!gScene) return;
    gCurrent=nullptr;
    authorEnsureAnimation();
    InvalidateRect(gWindow,nullptr,FALSE);
}

void authorRebuildBoneTrack(promptemon::bch::Animation& animation,const promptemon::bch::Bone& bone,AuthorBoneKeys& keyState) {
    if(keyState.keys.empty()) return;
    auto& track=authorEnsureTrack(animation,bone.name);
    const int endFrame=std::max(1,static_cast<int>(std::ceil(animation.frames)));
    const std::size_t count=static_cast<std::size_t>(endFrame)+1;
    track.translation.present=true;track.translation.constant=false;track.translation.start=0;track.translation.end=animation.frames;track.translation.samples.resize(count);
    track.rotation.present=true;track.rotation.constant=false;track.rotation.start=0;track.rotation.end=animation.frames;track.rotation.samples.resize(count);
    track.scale.present=true;track.scale.constant=false;track.scale.start=0;track.scale.end=animation.frames;track.scale.samples.resize(count);
    for(int frame=0;frame<=endFrame;++frame){
        const auto pose=authorInterpolateKeys(keyState,frame);
        const std::size_t i=static_cast<std::size_t>(frame);
        track.translation.samples[i]=pose.translation;
        track.rotation.samples[i]=pose.rotation;
        track.scale.samples[i]=pose.scale;
    }
}

void authorEnsureBoundaryKeys(promptemon::bch::Animation& animation,const promptemon::bch::Bone& bone,AuthorBoneKeys& state) {
    if(!state.keys.empty()) return;
    const int endFrame=std::max(1,static_cast<int>(std::ceil(animation.frames)));
    const auto first=authorPoseAt(animation,bone,0.0f);
    const auto last=authorPoseAt(animation,bone,static_cast<float>(endFrame));
    authorPutKey(state,{0,first.translation,first.rotation,first.scale});
    if(endFrame>0) authorPutKey(state,{endFrame,last.translation,last.rotation,last.scale});
}

void authorSetCurrentKey(const AuthorPose& pose) {
    auto* animation=authorEnsureAnimation();
    if(!animation||!gScene||gScene->bones().empty()) return;
    gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
    const auto& bone=gScene->bones()[gEditorBone];
    auto& state=gAuthorKeys[authorClipKey(*animation)][bone.name];
    authorEnsureBoundaryKeys(*animation,bone,state);
    const int frame=std::clamp(static_cast<int>(std::lround(gFrame)),0,std::max(1,static_cast<int>(std::ceil(animation->frames))));
    authorPutKey(state,{frame,pose.translation,pose.rotation,pose.scale});
    authorRebuildBoneTrack(*animation,bone,state);
    const std::size_t index=authorCurrentAnimationIndex();
    if(index!=static_cast<std::size_t>(-1)) gCurrent=&gScene->animations()[index];
    gFrame=static_cast<float>(frame);gAnimationPaused=true;
    setStatus("Keyframe: "+bone.name+" @ "+std::to_string(frame));
}

void authorAddCurrentKey() {
    auto* animation=authorEnsureAnimation();
    if(!animation||!gScene||gScene->bones().empty()) return;
    gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
    const auto pose=authorPoseAt(*animation,gScene->bones()[gEditorBone],gFrame);
    authorSetCurrentKey(pose);
}

void authorAdjustTranslation(int axis,float delta) {
    auto* animation=authorEnsureAnimation();
    if(!animation||!gScene||gScene->bones().empty()) return;
    gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
    auto pose=authorPoseAt(*animation,gScene->bones()[gEditorBone],gFrame);
    float* values[3]={&pose.translation.x,&pose.translation.y,&pose.translation.z};
    *values[axis]+=editStep(delta);
    authorSetCurrentKey(pose);
}

void authorAdjustRotation(int axis,float delta) {
    auto* animation=authorEnsureAnimation();
    if(!animation||!gScene||gScene->bones().empty()) return;
    gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
    auto pose=authorPoseAt(*animation,gScene->bones()[gEditorBone],gFrame);
    auto e=authorEulerFromQuat(pose.rotation);
    float* values[3]={&e.x,&e.y,&e.z};
    *values[axis]+=editStep(delta);
    pose.rotation=authorQuatFromEuler(e);
    authorSetCurrentKey(pose);
}

void authorAdjustScale(int axis,float delta) {
    auto* animation=authorEnsureAnimation();
    if(!animation||!gScene||gScene->bones().empty()) return;
    gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
    auto pose=authorPoseAt(*animation,gScene->bones()[gEditorBone],gFrame);
    float* values[3]={&pose.scale.x,&pose.scale.y,&pose.scale.z};
    *values[axis]=std::max(.01f,*values[axis]+editStep(delta));
    authorSetCurrentKey(pose);
}

void authorPaintKeyMarkers(HDC dc,const EditorLayout&l) {
    if(!gScene||!gCurrent||gScene->bones().empty()||IsRectEmpty(&l.timeline)) return;
    gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
    const auto clipIt=gAuthorKeys.find(authorClipKey(*gCurrent));
    if(clipIt==gAuthorKeys.end()) return;
    const auto boneIt=clipIt->second.find(gScene->bones()[gEditorBone].name);
    if(boneIt==clipIt->second.end()) return;
    RECT track{l.timeline.left+12,l.timeline.top+39,l.timeline.right-12,l.timeline.top+68};
    const float frames=std::max(1.0f,gCurrent->frames);
    for(const auto&key:boneIt->second.keys){
        const int x=track.left+static_cast<int>(std::clamp(key.frame/frames,0.0f,1.0f)*ew(track));
        RECT mark{x-2,track.top+3,x+3,track.top+9};eFill(dc,mark,RGB(240,240,240));
    }
}

void authorPaintAnimateInspector(HDC dc,const EditorLayout&l) {
    if(!gScene) return;
    eBox(dc,l.right,E_PANEL);
    int y=l.right.top+10;
    eSection(dc,l.right,y,"Playback / clip");
    RECT newClip{l.right.left+12,y,l.right.left+153,y+27};
    RECT playButton{newClip.right+4,y,l.right.right-12,y+27};
    eButton(dc,newClip,"New clip");
    eButton(dc,playButton,gAnimationPaused?"Play":"Pause",gCurrent!=nullptr,gCurrent!=nullptr);y+=33;
    eProp(dc,l.right,y,"Clip",gCurrent?gCurrent->name:"Base pose");
    eProp(dc,l.right,y,"Frame",std::to_string(static_cast<int>(std::lround(gFrame))));
    RECT first{l.right.left+12,y,l.right.left+72,y+25};
    RECT prev{first.right+4,y,first.right+64,y+25};
    RECT next{prev.right+4,y,prev.right+64,y+25};
    RECT last{next.right+4,y,l.right.right-12,y+25};
    eButton(dc,first,"|<",false,gCurrent!=nullptr);eButton(dc,prev,"-1",false,gCurrent!=nullptr);eButton(dc,next,"+1",false,gCurrent!=nullptr);eButton(dc,last,">|",false,gCurrent!=nullptr);y+=31;
    y+=3;eSection(dc,l.right,y,"Bone / auto key");
    if(!gScene->bones().empty()){
        gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
        RECT bprev{l.right.left+12,y,l.right.left+38,y+25};RECT bnext{l.right.right-38,y,l.right.right-12,y+25};RECT bname{bprev.right+4,y,bnext.left-4,y+25};
        eButton(dc,bprev,"<");eButton(dc,bnext,">");eBox(dc,bname,E_PANEL_ALT);
        const auto&bone=gScene->bones()[gEditorBone];
        eText(dc,bone.name.empty()?"joint "+std::to_string(gEditorBone):bone.name,bname,gEditorSmallFont,E_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);y+=31;
        AuthorPose pose; if(gCurrent) pose=authorPoseAt(*gCurrent,bone,gFrame); else {pose.translation=bone.bindTranslation;pose.rotation=authorQuatFromEuler(bone.bindRotation);pose.scale=bone.bindScale;}
        const auto rot=authorEulerFromQuat(pose.rotation);
        eAdjust(dc,l.right,y,"Position X",pose.translation.x);y+=25;
        eAdjust(dc,l.right,y,"Position Y",pose.translation.y);y+=25;
        eAdjust(dc,l.right,y,"Position Z",pose.translation.z);y+=25;
        eAdjust(dc,l.right,y,"Rotation X",rot.x,true);y+=25;
        eAdjust(dc,l.right,y,"Rotation Y",rot.y,true);y+=25;
        eAdjust(dc,l.right,y,"Rotation Z",rot.z,true);y+=25;
        eAdjust(dc,l.right,y,"Scale X",pose.scale.x);y+=25;
        eAdjust(dc,l.right,y,"Scale Y",pose.scale.y);y+=25;
        eAdjust(dc,l.right,y,"Scale Z",pose.scale.z);y+=29;
        RECT setKey{l.right.left+12,y,l.right.right-12,y+25};eButton(dc,setKey,"Set keyframe",false,true);y+=31;
    }
    eSection(dc,l.right,y,"Animations");
    const int visible=std::max<int>(1,static_cast<int>((l.right.bottom-y-8)/EDIT_ROW_H));
    const std::size_t total=gScene->animations().size()+1;
    const std::size_t maxScroll=total>static_cast<std::size_t>(visible)?total-static_cast<std::size_t>(visible):0;
    gAnimationScroll=std::min(gAnimationScroll,maxScroll);
    for(int i=0;i<visible;++i){
        const std::size_t idx=gAnimationScroll+static_cast<std::size_t>(i);if(idx>=total)break;
        RECT row{l.right.left+10,y+i*EDIT_ROW_H,l.right.right-10,y+(i+1)*EDIT_ROW_H};
        bool active=false;std::string label;
        if(idx==0){label="Base pose";active=gCurrent==nullptr;}else{const auto&a=gScene->animations()[idx-1];label=a.name+"  ("+std::to_string(static_cast<int>(std::ceil(a.frames)))+"f)";active=gCurrent==&a;}
        if(active)eFill(dc,row,E_ACTIVE);eText(dc,label,row,gEditorSmallFont,active?E_TEXT:RGB(198,198,198));
    }
}

void authorPaintEditor(HWND hwnd) {
    paintEditor(hwnd);
    if(gEditorMode!=EditorMode::Animate||!gScene) return;
    HDC dc=GetDC(hwnd);if(!dc)return;
    const auto l=editorLayout(hwnd);
    authorPaintAnimateInspector(dc,l);
    authorPaintKeyMarkers(dc,l);
    ReleaseDC(hwnd,dc);
}

void authorHandleAnimateClick(const EditorLayout&l,int x,int y) {
    if(!gScene) return;
    int cy=l.right.top+10+30;
    RECT newClip{l.right.left+12,cy,l.right.left+153,cy+27};RECT playButton{newClip.right+4,cy,l.right.right-12,cy+27};
    if(eHit(newClip,x,y)){authorCreateAnimation();return;}
    if(eHit(playButton,x,y)&&gCurrent){gAnimationPaused=!gAnimationPaused;InvalidateRect(gWindow,nullptr,FALSE);return;}
    cy+=33+48;
    RECT first{l.right.left+12,cy,l.right.left+72,cy+25};RECT prev{first.right+4,cy,first.right+64,cy+25};RECT next{prev.right+4,cy,prev.right+64,cy+25};RECT last{next.right+4,cy,l.right.right-12,cy+25};
    if(gCurrent&&(eHit(first,x,y)||eHit(prev,x,y)||eHit(next,x,y)||eHit(last,x,y))){
        if(eHit(first,x,y))gFrame=0;else if(eHit(last,x,y))gFrame=std::max(1.0f,gCurrent->frames);else if(eHit(prev,x,y))gFrame=std::max(0.0f,gFrame-1.0f);else gFrame=std::min(std::max(1.0f,gCurrent->frames),gFrame+1.0f);
        gAnimationPaused=true;InvalidateRect(gWindow,nullptr,FALSE);return;
    }
    cy+=31+3+30;
    if(!gScene->bones().empty()){
        gEditorBone=std::min(gEditorBone,gScene->bones().size()-1);
        RECT bprev{l.right.left+12,cy,l.right.left+38,cy+25};RECT bnext{l.right.right-38,cy,l.right.right-12,cy+25};
        if(eHit(bprev,x,y)||eHit(bnext,x,y)){if(eHit(bprev,x,y))gEditorBone=gEditorBone?gEditorBone-1:gScene->bones().size()-1;else gEditorBone=(gEditorBone+1)%gScene->bones().size();InvalidateRect(gWindow,nullptr,FALSE);return;}
        cy+=31;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustTranslation(0,-.02f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustTranslation(0,.02f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustTranslation(1,-.02f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustTranslation(1,.02f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustTranslation(2,-.02f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustTranslation(2,.02f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustRotation(0,-PI_F/90.0f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustRotation(0,PI_F/90.0f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustRotation(1,-PI_F/90.0f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustRotation(1,PI_F/90.0f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustRotation(2,-PI_F/90.0f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustRotation(2,PI_F/90.0f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustScale(0,-.05f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustScale(0,.05f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustScale(1,-.05f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustScale(1,.05f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=25;
        if(eHit(minusBox(l.right,cy),x,y)){authorAdjustScale(2,-.05f);InvalidateRect(gWindow,nullptr,FALSE);return;}if(eHit(plusBox(l.right,cy),x,y)){authorAdjustScale(2,.05f);InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=29;
        RECT setKey{l.right.left+12,cy,l.right.right-12,cy+25};if(eHit(setKey,x,y)){authorAddCurrentKey();InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=31;
    }
    cy+=30;
    if(y<cy)return;
    const std::size_t idx=gAnimationScroll+static_cast<std::size_t>((y-cy)/EDIT_ROW_H);
    if(idx==0){gCurrent=nullptr;gFrame=0;gAnimationPaused=true;}
    else if(idx-1<gScene->animations().size()){gCurrent=&gScene->animations()[idx-1];gFrame=0;gAnimationPaused=false;}
    InvalidateRect(gWindow,nullptr,FALSE);
}

std::string editorJsonEscape(const std::string& value) {
    std::ostringstream out;
    for(unsigned char c:value){
        switch(c){case '\\':out<<"\\\\";break;case '"':out<<"\\\"";break;case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;default:if(c<0x20)out<<'?';else out<<static_cast<char>(c);}
    }
    return out.str();
}

void writeEditedAnimations(const std::filesystem::path& directory) {
    if(!gScene)return;
    const auto outDir=directory/"animations";std::filesystem::create_directories(outDir);
    std::ofstream out(outDir/"edited-animations.json");if(!out)throw std::runtime_error("Could not write edited animations");
    out<<"{\n  \"format\": \"smoke-detector-animation-v1\",\n  \"fps\": 30,\n  \"animations\": [\n";
    const auto&animations=gScene->animations();
    for(std::size_t ai=0;ai<animations.size();++ai){const auto&a=animations[ai];
        out<<"    {\"name\":\""<<editorJsonEscape(a.name)<<"\",\"frames\":"<<a.frames<<",\"loop\":"<<(a.loop?"true":"false")<<",\"tracks\":[";
        for(std::size_t ti=0;ti<a.tracks.size();++ti){const auto&t=a.tracks[ti];if(ti)out<<',';out<<"\n      {\"bone\":\""<<editorJsonEscape(t.boneName)<<"\"";
        auto writeVec=[&](const char*name,const promptemon::bch::Vec3Track&v){out<<",\""<<name<<"\":{\"present\":"<<(v.present?"true":"false")<<",\"constant\":"<<(v.constant?"true":"false")<<",\"start\":"<<v.start<<",\"end\":"<<v.end<<",\"samples\":[";for(std::size_t i=0;i<v.samples.size();++i){if(i)out<<',';out<<'['<<v.samples[i].x<<','<<v.samples[i].y<<','<<v.samples[i].z<<']';}out<<"]}";};
        auto writeQuat=[&](const char*name,const promptemon::bch::QuatTrack&q){out<<",\""<<name<<"\":{\"present\":"<<(q.present?"true":"false")<<",\"constant\":"<<(q.constant?"true":"false")<<",\"start\":"<<q.start<<",\"end\":"<<q.end<<",\"samples\":[";for(std::size_t i=0;i<q.samples.size();++i){if(i)out<<',';out<<'['<<q.samples[i].x<<','<<q.samples[i].y<<','<<q.samples[i].z<<','<<q.samples[i].w<<']';}out<<"]}";};
        writeVec("translation",t.translation);writeQuat("rotation",t.rotation);writeVec("scale",t.scale);out<<"}";}
        if(!a.tracks.empty())out<<'\n';out<<"    ]}"<<(ai+1<animations.size()?",":"")<<"\n";
    }
    out<<"  ]\n}\n";
}

void writeEditorObjAndState(const std::filesystem::path& modelDir) {
    if (!gScene) return;
    std::filesystem::create_directories(modelDir);
    std::filesystem::create_directories(modelDir.parent_path() / "textures");

    auto safeEditorName = [](std::string value) {
        for (char& c : value) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (!(std::isalnum(u) || c == '-' || c == '_' || c == '.')) c = '_';
        }
        return value.empty() ? std::string("asset") : value;
    };

    std::unordered_map<std::string, std::string> textureFiles;
    const std::size_t oldMaterial = gEditorMaterial;
    auto& layers = currentImageLayers();
    for (std::size_t i = 0; i < gScene->materials().size(); ++i) {
        const auto& material = gScene->materials()[i];
        if (material.textureName.empty()) continue;
        if (i < layers.size() && !layers[i].path.empty()) {
            gEditorMaterial = i;
            gLayerBitmap.reset();
            gLayerBitmapPath.clear();
            auto bitmap = compositeTextureBitmap();
            if (bitmap) {
                const std::string filename = safeEditorName(material.textureName) + ".png";
                if (savePng(*bitmap, modelDir.parent_path() / "textures" / filename)) {
                    textureFiles[material.textureName] = filename;
                    continue;
                }
            }
        }
        textureFiles[material.textureName] = safeEditorName(material.textureName) + ".tga";
    }
    gEditorMaterial = oldMaterial;
    gLayerBitmap.reset();
    gLayerBitmapPath.clear();

    std::ofstream obj(modelDir / "model.obj");
    std::ofstream mtl(modelDir / "model.mtl");
    if (!obj || !mtl) throw std::runtime_error("Could not write edited OBJ");
    obj << "mtllib model.mtl\n";

    const auto matrices = editorSkinMatrices();
    const auto& meshEdits = currentMeshEdits();
    std::size_t base = 1;
    for (std::size_t mi = 0; mi < gScene->meshes().size(); ++mi) {
        if (mi < meshEdits.size() && !meshEdits[mi].visible) continue;
        const auto& mesh = gScene->meshes()[mi];
        const MeshEdit edit = mi < meshEdits.size() ? meshEdits[mi] : MeshEdit{};
        const auto pivot = meshCenter(mesh, matrices);
        const Material* material = mesh.materialId < gScene->materials().size()
            ? &gScene->materials()[mesh.materialId] : nullptr;
        const UvFix* fix = uvFixForMaterial(mesh.materialId);
        const std::string materialName = safeEditorName(material && !material->name.empty()
            ? material->name : "material_" + std::to_string(mi));

        obj << "o " << safeEditorName(mesh.name.empty() ? "mesh_" + std::to_string(mi) : mesh.name) << "\n";
        obj << "usemtl " << materialName << "\n";
        for (const auto& vertex : mesh.vertices) {
            auto p = gScene->skin(vertex, matrices);
            p = applyMeshEdit(p, edit, pivot);
            obj << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            const auto uv = adjustedUv(material, vertex.uv, fix);
            obj << "vt " << uv.x << ' ' << (1.0f - uv.y) << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            obj << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << "\n";
        }
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            obj << "f";
            for (int k = 0; k < 3; ++k) {
                const std::size_t n = base + i + static_cast<std::size_t>(k);
                obj << ' ' << n << '/' << n << '/' << n;
            }
            obj << "\n";
        }

        mtl << "newmtl " << materialName << "\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\n";
        if (material && !material->textureName.empty()) {
            const auto it = textureFiles.find(material->textureName);
            if (it != textureFiles.end()) mtl << "map_Kd ../textures/" << it->second << "\n";
        }
        mtl << "\n";
        base += mesh.vertices.size();
    }

    std::ofstream state(modelDir / "editor-state.json");
    if (!state) return;
    state << "{\n  \"model\": \"" << editorJsonEscape(gScene->modelName().empty() ? "model" : gScene->modelName()) << "\",\n";
    state << "  \"edited_animations\": \"../animations/edited-animations.json\",\n";
    state << "  \"meshes\": [\n";
    for (std::size_t i = 0; i < meshEdits.size(); ++i) {
        const auto& e = meshEdits[i];
        state << "    {\"index\":" << i << ",\"visible\":" << (e.visible ? "true" : "false")
              << ",\"position\":[" << e.tx << ',' << e.ty << ',' << e.tz << ']'
              << ",\"rotation\":[" << e.rx << ',' << e.ry << ',' << e.rz << ']'
              << ",\"scale\":[" << e.sx << ',' << e.sy << ',' << e.sz << "]}"
              << (i + 1 < meshEdits.size() ? "," : "") << "\n";
    }
    state << "  ],\n  \"bones\": [\n";
    const auto& boneEdits = currentBoneEdits();
    for (std::size_t i = 0; i < boneEdits.size(); ++i) {
        const auto& e = boneEdits[i];
        const std::string name = i < gScene->bones().size() ? gScene->bones()[i].name : std::string{};
        state << "    {\"index\":" << i << ",\"name\":\"" << editorJsonEscape(name)
              << "\",\"position\":[" << e.tx << ',' << e.ty << ',' << e.tz << ']'
              << ",\"rotation\":[" << e.rx << ',' << e.ry << ',' << e.rz << "]}"
              << (i + 1 < boneEdits.size() ? "," : "") << "\n";
    }
    state << "  ],\n  \"materials\": [\n";
    const auto& fixes = currentUvFixes();
    for (std::size_t i = 0; i < gScene->materials().size(); ++i) {
        const auto& material = gScene->materials()[i];
        const UvFix fix = i < fixes.size() ? fixes[i] : UvFix{};
        const ImageLayerState layer = i < layers.size() ? layers[i] : ImageLayerState{};
        state << "    {\"index\":" << i << ",\"name\":\"" << editorJsonEscape(material.name)
              << "\",\"texture\":\"" << editorJsonEscape(material.textureName)
              << "\",\"uv\":{\"transform\":" << fix.transformType
              << ",\"offset\":[" << fix.offsetU << ',' << fix.offsetV << ']'
              << ",\"scale\":[" << fix.scaleU << ',' << fix.scaleV << ']'
              << ",\"rotation\":" << fix.rotation << "},\"image_layer\":{\"path\":\""
              << editorJsonEscape(std::filesystem::path(layer.path).filename().string())
              << "\",\"offset\":[" << layer.offsetU << ',' << layer.offsetV << ']'
              << ",\"scale\":" << layer.scale << ",\"rotation\":" << layer.rotation
              << ",\"crop\":[" << layer.cropX << ',' << layer.cropY << ',' << layer.cropW << ',' << layer.cropH << "]}}"
              << (i + 1 < gScene->materials().size() ? "," : "") << "\n";
    }
    state << "  ]\n}\n";
}

void exportEditorSelected() {
    if (gSelectedItem >= gItems.size()) return;
    const auto& item = gItems[gSelectedItem];
    const std::string model = item.category == 1 ? modelPathFor(item) : std::string{};
    if (model.empty() || !gArchive || !gScene || gSelectedModel != model) {
        exportStudioSelected();
        return;
    }

    const auto result = AssetExporter::exportModel(*gArchive, model, gExportRoot);
    writeEditorObjAndState(result.interchangeModel.parent_path());
    writeEditedAnimations(result.directory);
    setStatus("Edited package exported: " + result.directory.string());
    ShellExecuteA(gWindow, "open", result.directory.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

#define paintEditor authorPaintEditor
#define handleAnimateInspectorClick authorHandleAnimateClick
