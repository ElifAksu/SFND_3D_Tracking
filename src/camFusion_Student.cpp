
#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        { 
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0; 
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {   
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
           // cout << "top-bottom" << top << " "<< bottom<<endl;

        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);
        //cout << "boundingbox geldi" <<endl;
        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-250, bottom+50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-250, bottom+125), cv::FONT_ITALIC, 2, currColor);  
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }

    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);

    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
   for (auto it1 = kptsCurr.begin(); it1 != kptsCurr.end(); ++it1)
    {
        if (boundingBox.roi.contains((*it1).pt))
        {
            boundingBox.keypoints.push_back((*it1));
        }
    }

    for (auto it2 = kptMatches.begin(); it2 != kptMatches.end(); ++it2)
    {
        if (boundingBox.roi.contains(kptsCurr[(*it2).trainIdx].pt))
        {
            boundingBox.kptMatches.push_back((*it2));
        }
    }
}


// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
   vector<double> distRatios; // stores the distance ratios for all keypoints between curr. and prev. frame
    for (auto it1 = kptMatches.begin(); it1 != kptMatches.end() - 1; ++it1)
    { // outer kpt. loop

        // get current keypoint and its matched partner in the prev. frame
        cv::KeyPoint kpOuterCurr = kptsCurr.at(it1->trainIdx);
        cv::KeyPoint kpOuterPrev = kptsPrev.at(it1->queryIdx);

        for (auto it2 = kptMatches.begin() + 1; it2 != kptMatches.end(); ++it2)
        { // inner kpt.-loop

            double minDist = 100.0; // min. required distance

            // get next keypoint and its matched partner in the prev. frame
            cv::KeyPoint kpInnerCurr = kptsCurr.at(it2->trainIdx);
            cv::KeyPoint kpInnerPrev = kptsPrev.at(it2->queryIdx);

            // compute distances and distance ratios
            double distCurr = cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
            double distPrev = cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);

            if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist)
            { // avoid division by zero

                double distRatio = distCurr / distPrev;
                distRatios.push_back(distRatio);
            }
        } // eof inner loop over all matched kpts
    }     // eof outer loop over all matched kpts

    // only continue if list of distance ratios is not empty
    if (distRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }

    // compute camera-based TTC from distance ratios
    double meanDistRatio = std::accumulate(distRatios.begin(), distRatios.end(), 0.0) / distRatios.size();

    double dT = 1 / frameRate;
    //TTC = -dT / (1 - meanDistRatio);

    // STUDENT TASK (replacement for meanDistRatio)
    size_t size=distRatios.size();
    sort(distRatios.begin(), distRatios.end());
    double medianDistRatio = 0;
    if (size % 2 == 0)
    {
      medianDistRatio = (distRatios[size / 2 - 1] + distRatios[size / 2]) / 2;
    }
    else 
    {
      medianDistRatio = distRatios[size / 2];
    }
    TTC = -dT / (1 - medianDistRatio);
    cout<<"TTC from Camera " << TTC <<endl;
    
}


void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
   double dT = 1/frameRate; // time between two measurements in seconds
    vector<double> XPrev;
    vector<double> XCurr;
    // find closest distance to Lidar points 
    double minXPrev = 1e9, minXCurr = 1e9;

    for(auto it=lidarPointsPrev.begin(); it!=lidarPointsPrev.end(); ++it) {
        minXPrev = minXPrev>it->x ? it->x : minXPrev;
        XPrev.push_back(it->x);
    }

    for(auto it=lidarPointsCurr.begin(); it!=lidarPointsCurr.end(); ++it) {
        minXCurr = minXCurr>it->x ? it->x : minXCurr;
        XCurr.push_back(it->x);
    }

    
    sort(XPrev.begin(), XPrev.end());
    sort(XCurr.begin(), XCurr.end());
    int XPrevSize=XPrev.size();
    int XCurrSize=XCurr.size();
    double medianPrevX = 0;
    if (XPrevSize % 2 == 0)
    {
      medianPrevX = (XPrev[XPrevSize / 2 - 1] + XPrev[XPrevSize / 2]) / 2;
    }
    else 
    {
      medianPrevX = XPrev[XPrevSize / 2];
    }
    double medianCurrX = 0;
    if (XPrevSize % 2 == 0)
    {
      medianCurrX = (XCurr[XCurrSize / 2 - 1] + XCurr[XPrevSize / 2]) / 2;
    }
    else 
    {
      medianCurrX = XCurr[XCurrSize / 2];
    }

    if (XPrev.size() == 0)
    {
        TTC = NAN;
        return;
    }
    if (XCurr.size() == 0)
    {
        TTC = NAN;
        return;
    }

    // compute TTC from both measurements
    //TTC = minXCurr * dT / std::abs(minXPrev-minXCurr);
    TTC = medianCurrX * dT / std::abs(medianPrevX-medianCurrX);
    
    cout<<"TTC from Lidar " << TTC <<endl;
    return;
}


void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{
    vector<pair<int,int>> BoxIDs;
    cv::KeyPoint prevKP; 
    cv::KeyPoint currentKP;
    cout<< "prev frame box number: " <<prevFrame.boundingBoxes.size() << endl;
    cout<< "curr frame box number: " <<currFrame.boundingBoxes.size() << endl;
    BoxIDs.clear();


    for(auto match : matches)
    {
        int PrevBoxID = -1;
        int CurrentBoxID =-1;
        int prevIdx = match.queryIdx;           //matched index in prev frame
        int currentIdx = match.trainIdx;        //matched index in curr frame
        prevKP = prevFrame.keypoints[prevIdx];  //matched keypoint in prev frame
        currentKP = currFrame.keypoints[currentIdx]; //matched keypoint in current frame
        for(auto bboxesPrev : prevFrame.boundingBoxes )
        {
            if(bboxesPrev.roi.contains(prevKP.pt))
               {
                PrevBoxID = bboxesPrev.boxID;
               }

        }
         for(auto bboxesCurrent : currFrame.boundingBoxes )
        {
            if(bboxesCurrent.roi.contains(currentKP.pt))
               {
                CurrentBoxID = bboxesCurrent.boxID;
               }
            
        }
        BoxIDs.push_back(make_pair(PrevBoxID,CurrentBoxID));    
       // cout<<"matched box ID prev :" << PrevBoxID <<" current: " <<CurrentBoxID<< endl; 
    }
    
    for(auto bboxesCurrent : currFrame.boundingBoxes )
    {   vector<int> prevID_vec;
        int max =15; int mostvalue = -100;
        cout<<" curr frame box id :" << bboxesCurrent.boxID <<endl;
        cout<<" BOXIDS size :" << BoxIDs.size() <<endl;
        
        for(int i =0; i <BoxIDs.size(); i++)
        {
            if(BoxIDs[i].second == bboxesCurrent.boxID)
            {
                prevID_vec.push_back(BoxIDs[i].first);
                //cout<<" matches :" << BoxIDs[i].first << " "<<BoxIDs[i].second <<endl;
            }
            
            
        }
        for(int m =0; m< prevID_vec.size(); m++)
            {
                int countoutput = (int)count(prevID_vec.begin(),prevID_vec.end(), prevID_vec[m]);
                if(countoutput > max && prevID_vec[m] != -1)
                {
                    max =countoutput;
                    mostvalue = prevID_vec[m];
                }
            }
        if(mostvalue != -100)
        {
            bbBestMatches.insert({mostvalue, bboxesCurrent.boxID });
            cout<<"best matches :" << mostvalue << " " << bboxesCurrent.boxID <<endl; 
        }
        
        

    }
    /*cv::Mat visImg = prevFrame.cameraImg.clone();
        for(auto it=prevFrame.boundingBoxes.begin(); it!=prevFrame.boundingBoxes.end(); ++it) {
            
            // Draw rectangle displaying the bounding box
            int top, left, width, height;
            top = (*it).roi.y;
            left = (*it).roi.x;
            width = (*it).roi.width;
            height = (*it).roi.height;
            cv::rectangle(visImg, cv::Point(left, top), cv::Point(left+width, top+height),cv::Scalar(0, 255, 0), 2);
            
            //string label = cv::format("%.2f", (*it).boxID);
            //label = classes[((*it).classID)] + ":" + label;
            char label[200];
            sprintf(label, "id=%d", (*it).boxID);
            // Display label at the top of the bounding box
            int baseLine;
            cv::Size labelSize = getTextSize(label, cv::FONT_ITALIC, 0.5, 1, &baseLine);
            top = max(top, labelSize.height);
            rectangle(visImg, cv::Point(left, top - round(1.5*labelSize.height)), cv::Point(left + round(1.5*labelSize.width), top + baseLine), cv::Scalar(255, 255, 255), cv::FILLED);
            cv::putText(visImg, label, cv::Point(left, top), cv::FONT_ITALIC, 0.75, cv::Scalar(0,0,0),1);
            
        }
        
       string windowName = "Prev Frame";
        cv::namedWindow( windowName, 1 );
        cv::imshow( windowName, visImg );
        cv::waitKey(0); // wait for key to be pressed

        cv::Mat visImg_ = currFrame.cameraImg.clone();
        for(auto it=currFrame.boundingBoxes.begin(); it!=currFrame.boundingBoxes.end(); ++it) {
            
            // Draw rectangle displaying the bounding box
            int top, left, width, height;
            top = (*it).roi.y;
            left = (*it).roi.x;
            width = (*it).roi.width;
            height = (*it).roi.height;
            cv::rectangle(visImg_, cv::Point(left, top), cv::Point(left+width, top+height),cv::Scalar(0, 255, 0), 2);
            
            //string label = cv::format("%.2f", (*it).boxID);
            //label = classes[((*it).classID)] + ":" + label;
            char label[200];
            sprintf(label, "id=%d", (*it).boxID);
            // Display label at the top of the bounding box
            int baseLine;
            cv::Size labelSize = getTextSize(label, cv::FONT_ITALIC, 0.5, 1, &baseLine);
            top = max(top, labelSize.height);
            rectangle(visImg_, cv::Point(left, top - round(1.5*labelSize.height)), cv::Point(left + round(1.5*labelSize.width), top + baseLine), cv::Scalar(255, 255, 255), cv::FILLED);
            cv::putText(visImg_, label, cv::Point(left, top), cv::FONT_ITALIC, 0.75, cv::Scalar(0,0,0),1);
            
        }
        
        string windowName2 = "Curr Frame";
        cv::namedWindow( windowName2, 1 );
        cv::imshow( windowName2, visImg_ );
        cv::waitKey(0); // wait for key to be pressed 
      */

}
