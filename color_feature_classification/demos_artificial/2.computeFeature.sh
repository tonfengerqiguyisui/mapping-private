#!/bin/bash
# Example directory containing .pcd files
DATA=`pwd`/data/
n=0

#for i in `find $DATA -type f \( -iname "obj_*.pcd" ! -iname "*GRSD_CCHLAC.pcd" ! -iname "*colorCHLAC.pcd" \) | sort -d`
for i in `find $DATA -type f \( -iname "noisy_*.pcd" ! -iname "*GRSD_CCHLAC.pcd" ! -iname "*colorCHLAC.pcd" \) | sort -d`
do
    echo $i
    num=$(printf "%03d" $n)
    dir_name=$(printf "obj%03d" $n)
    #echo $dir_name
    rosrun color_feature_classification computeFeature $i c test_features_c/$num.pcd
    rosrun color_feature_classification computeFeature $i d test_features_d/$num.pcd
    rosrun color_feature_classification computeFeature $i g test_features_g/$num.pcd
    rosrun color_feature_classification computeFeature $i r test_features_r/$num.pcd
    n=`expr $n + 1`
done