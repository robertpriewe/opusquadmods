/* Extracted established native sidebar policy, including R110 chooser placement. */
static const char *const fx_readout_classes[4]={
    "N3gui2fx14SelectFxWidgetE", "N3gui2fx13BpmInfoWidgetE",
    "N3gui2fx18QuantizeInfoWidgetE", "N3gui2fx19BeatTimeFieldWidgetE"
};
static struct fx_sidebar_state {
    char *root,*owner,*parent,*part[4];
    char *time,*beat,*percent;
    int time_box[4],time_visible;
    int root_box[4],part_box[4][4],visible,index;
    char *chooser;
    int chooser_index,chooser_box[4],chooser_screen[4];
} g_fx_sidebar;

static void fx_sidebar_restore(void) {
    struct fx_sidebar_state old=g_fx_sidebar;
    memset(&g_fx_sidebar,0,sizeof(g_fx_sidebar));
    if(!old.root || !is_class(old.root,BEATFX_CLASS))return;
    char *parent=*(char **)(old.root+COMP_PARENT);
    if(parent!=old.owner && parent!=old.parent)return;
    if(is_class(old.chooser,"N3gui2fx18SelectFxListWidgetE") &&
       *(char **)(old.chooser+COMP_PARENT)==old.owner &&
       is_class(old.part[0],fx_readout_classes[0])) {
        sb_components.reparent(old.part[0],old.chooser,old.chooser_index);
        comp_bounds(old.chooser,old.chooser_box[0],old.chooser_box[1],old.chooser_box[2],old.chooser_box[3]);
    }
    if(is_class(old.time,"N3gui2fx14TimeInfoWidgetE") &&
       *(char **)(old.time+COMP_PARENT)==old.part[3]) {
        const int *b=old.time_box;
        comp_bounds(old.time,b[0],b[1],b[2],b[3]);
        /* Stock format switching is exclusive between percent, beat and time
         * (0x19060c8..0x19062fc). Preserve the CURRENT format, not a stale entry
         * snapshot, if the user changed effects/formats while in this sidebar. */
        int visible=old.time_visible;
        if(is_class(old.beat,"N3gui2fx14BeatInfoWidgetE") &&
           is_class(old.percent,"N3gui2fx17PercentInfoWidgetE"))
            visible=!comp_visible_now(old.beat) && !comp_visible_now(old.percent);
        comp_visible(old.time,visible);
    }
    for(int i=0;i<4;i++)
        if(is_class(old.part[i],fx_readout_classes[i]) && *(char **)(old.part[i]+COMP_PARENT)==old.root) {
            const int *b=old.part_box[i];
            comp_bounds(old.part[i],b[0],b[1],b[2],b[3]);
        }
    /* Do not steal the component if another stock screen already adopted it. */
    if(*(char **)(old.root+COMP_PARENT)==old.owner &&
       is_class(old.parent,"N3gui5mixer15MixerAreaWidgetE"))
        sb_components.reparent(old.parent,old.root,old.index);
    if(*(char **)(old.root+COMP_PARENT)==old.parent) {
        const int *b=old.root_box;
        comp_bounds(old.root,b[0],b[1],b[2],b[3]);
        comp_visible(old.root,old.visible);
    }
}

static int fx_sidebar_converge(char *owner,char *mixer) {
    if(g_fx_sidebar.root && (g_fx_sidebar.owner!=owner || g_fx_sidebar.parent!=mixer))
        fx_sidebar_restore();
    if(!g_fx_sidebar.root) {
        char *root=child_of_class(mixer,BEATFX_CLASS,0),*parts[4];
        if(!root || *(char **)(root+COMP_PARENT)!=mixer)return 0;
        for(int i=0;i<4;i++) {
            parts[i]=child_of_class(root,fx_readout_classes[i],0);
            if(!parts[i] || *(char **)(parts[i]+COMP_PARENT)!=root)return 0;
        }
        /* The stock field already owns the live millisecond readout, normally
         * sharing the beat row. Keep its controller and give it a second row. */
        char *time=child_of_class(parts[3],"N3gui2fx14TimeInfoWidgetE",0);
        char *beat=child_of_class(parts[3],"N3gui2fx14BeatInfoWidgetE",0);
        char *percent=child_of_class(parts[3],"N3gui2fx17PercentInfoWidgetE",0);
        if(!time || !beat || !percent)return 0;
        char **kids=*(char ***)(mixer+0x40);
        int n=*(int *)(mixer+0x50),index=-1;
        if(!ptr_ok(kids) || n<0 || n>64)return 0;
        for(int i=0;i<n;i++)if(kids[i]==root)index=i;
        if(index<0)return 0;
        g_fx_sidebar.root=root;g_fx_sidebar.owner=owner;g_fx_sidebar.parent=mixer;
        g_fx_sidebar.index=index;g_fx_sidebar.visible=comp_visible_now(root);
        g_fx_sidebar.time=time;g_fx_sidebar.time_visible=comp_visible_now(time);
        g_fx_sidebar.beat=beat;g_fx_sidebar.percent=percent;
        memcpy(g_fx_sidebar.time_box,time+COMP_BOUNDS,sizeof(g_fx_sidebar.time_box));
        memcpy(g_fx_sidebar.root_box,root+COMP_BOUNDS,sizeof(g_fx_sidebar.root_box));
        for(int i=0;i<4;i++) {
            g_fx_sidebar.part[i]=parts[i];
            memcpy(g_fx_sidebar.part_box[i],parts[i]+COMP_BOUNDS,sizeof(g_fx_sidebar.part_box[i]));
        }
        /* The compact sidebar is background UI, never above native popups. */
        sb_components.reparent(owner,root,0);
        /* Keep the stock chooser OUTSIDE the compact/clipped readout. Its
         * controllers and visibility remain native; only parent/coordinates
         * change. Otherwise moving SelectFxWidget to -532 clips every effect
         * except a sliver of the last rows (reported as HELIX on hardware). */
        char *chooser=child_of_class(parts[0],"N3gui2fx18SelectFxListWidgetE",0);
        if(chooser) {
            char **ck=*(char ***)(parts[0]+0x40);int cn=*(int *)(parts[0]+0x50),ci=-1;
            if(ptr_ok(ck)&&cn>=0&&cn<=64)for(int i=0;i<cn;i++)if(ck[i]==chooser)ci=i;
            if(ci>=0){
                g_fx_sidebar.chooser=chooser;g_fx_sidebar.chooser_index=ci;
                memcpy(g_fx_sidebar.chooser_box,chooser+COMP_BOUNDS,sizeof g_fx_sidebar.chooser_box);
                int *q=g_fx_sidebar.chooser_screen;memcpy(q,g_fx_sidebar.chooser_box,sizeof g_fx_sidebar.chooser_box);
                q[0]+=g_fx_sidebar.root_box[0]+g_fx_sidebar.part_box[0][0]+sb_parent_box[0];
                q[1]+=g_fx_sidebar.root_box[1]+g_fx_sidebar.part_box[0][1]+sb_parent_box[1];
                char *p=*(char **)(mixer+COMP_PARENT);
                for(int n=0;p&&p!=owner&&n<4;n++,p=*(char **)(p+COMP_PARENT)){
                    const int *b=(const int *)(p+COMP_BOUNDS);q[0]+=b[0];q[1]+=b[1];
                }
                sb_components.reparent(owner,chooser,-1);
                comp_bounds(chooser,q[0],q[1],q[2],q[3]);
            }
        }
    }
    if(*(char **)(g_fx_sidebar.root+COMP_PARENT)!=owner) {
        fx_sidebar_restore();return 0;
    }
    for(int i=0;i<4;i++)
        if(!is_class(g_fx_sidebar.part[i],fx_readout_classes[i]) ||
           *(char **)(g_fx_sidebar.part[i]+COMP_PARENT)!=g_fx_sidebar.root) {
            fx_sidebar_restore();return 0;
        }
    if(!is_class(g_fx_sidebar.time,"N3gui2fx14TimeInfoWidgetE") ||
       *(char **)(g_fx_sidebar.time+COMP_PARENT)!=g_fx_sidebar.part[3]) {
        fx_sidebar_restore();return 0;
    }
    comp_bounds(g_fx_sidebar.root,1110,120,145,166);
    comp_bounds(g_fx_sidebar.part[0],0,-532,145,611);
    comp_bounds(g_fx_sidebar.part[1],29,12,116,24);
    comp_bounds(g_fx_sidebar.part[2],5,13,20,16);
    comp_bounds(g_fx_sidebar.part[3],0,90,145,68);
    comp_bounds(g_fx_sidebar.time,0,44,145,24);
    /* Percentage-based effects must not acquire a misleading millisecond row. */
    comp_visible(g_fx_sidebar.time,!comp_visible_now(g_fx_sidebar.percent));
    /* Visibility/popup arbitration stays with the shared host. */
    return 1;
}
