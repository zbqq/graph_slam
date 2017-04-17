/*
 * Feb 28, 2017 David Z 
 * 
 * test for PnP match with cov using SwissRanger 4000 
 *
 * */

#include <ros/ros.h>
#include <string> 
#include <tf/tf.h>
#include <iostream>
#include <pcl/common/transforms.h>
#include "sparse_feature_vo.h"
#include "SR_reader_cv.h"
#include "camera_node_pnp.h"
#include "pc_from_image.h"
#include "vtk_viewer.h"
#include <gtsam/geometry/Pose3.h>
#include "gtsam_graph.h"

using namespace cv; 
using namespace std; 
using namespace gtsam;

#define R2D(r) (((r)*180.)/M_PI)

void testTwoFrameMatch(); 
void map_raw_img_to_grey(unsigned short * pRaw, unsigned char* pGrey, int N);
void print_tf(ostream& out, tf::Transform tT); 
void fromTF2Eigen(tf::Transform tT, Eigen::Matrix<float, 4 , 4>& tr);

void addTwoNodes(); // similar to Match two frames, but through graph

Eigen::Matrix<double, 6, 1> cov_Helper(Eigen::Matrix4f& m)
{
  Eigen::Matrix4d md = m.cast<double>(); 
  Pose3 p(md); 
  gtsam::Vector6 r = Pose3::ChartAtOrigin::Local(p); 
  return r; 
} 

int main(int argc, char* argv[])
{
  ros::init(argc, argv, "vro_match_pnp"); 
  ros::NodeHandle n;
  testTwoFrameMatch(); 
  // addTwoNodes(); 
  return 0; 
}

void testTwoFrameMatch()
{
  // read sr4k data from disk 
  string src_file = "../../../src/visual_odometry/data/d1_0726.bdat"; 
  string tar_file = "../../../src/visual_odometry/data/d1_0724.bdat"; 
  
  ros::NodeHandle nh("~"); 
  nh.param("src_filename", src_file, src_file); 
  nh.param("tar_filename", tar_file, tar_file); 

  // CSReader r4k; 
  CSReadCV r4k; 
  sr_data src_data; 
  sr_data tar_data; 

  // generate imgs and dpts 
  cv::Mat tar_cv_d_img(SR_HEIGHT, SR_WIDTH, CV_16UC1); 
  cv::Mat src_cv_d_img(SR_HEIGHT, SR_WIDTH, CV_16UC1); 
  cv::Mat src_cv_i_img; 
  cv::Mat tar_cv_i_img; 

  if(!r4k.readOneFrameCV(src_file, src_cv_i_img, src_cv_d_img))
  {
    ROS_INFO("%s failed to read file %s", __FILE__, src_file.c_str()); 
    return ;
  }
  if(!r4k.readOneFrameCV(tar_file, tar_cv_i_img, tar_cv_d_img))
  {
    ROS_INFO("%s failed to read file %s", __FILE__, tar_file.c_str()); 
    return ;
  }
 
  // display imgs 
  cv::imshow("intensity img", tar_cv_i_img); 
  cv::waitKey(500); 
  cv::imshow("depth img", tar_cv_d_img); 
  cv::waitKey(0); 
  
  // parameters configure 
  CParams* pP = CParams::Instance(); 
  pP->m_feature_detector_type = "SIFT";  // SIFTGPU 
  pP->m_feature_descriptor_type = "SIFT";  // SIFTGPU
  pP->m_feature_match_type = "FLANN"; 
  pP->m_nn_distance_ratio = 0.5; // 0.95 for SIFTGPU, 0.5-0.7 for SIFT 
  pP->m_max_dist_for_inliers = 0.05;  
  pP->m_max_num_features = 500; 
  pP->m_ransac_iterations = 5000; 

  // VRO 
  CamModel sr4k(250.5773, 250.5773, 90, 70, -0.8466, 0.5370); 
  CCameraNode::set_cam_cov(sr4k); 
  sr4k.z_offset = 0.015;  // this is only for sr4k 
  // CSparseFeatureVO vo(sr4k, "SIFTGPU", "SIFTGPU", "FLANN");
  CSparseFeatureVO vo(sr4k); 

  Eigen::Matrix<double, 6, 6> cov ; 

  CCameraNode::gb_dis_match_point = true; // whether to see the result of 

  // tf::Transform tran = vo.VRO( tar_cv_i_img, tar_cv_d_img, src_cv_i_img, src_cv_d_img, 0.001, cov_Helper, &cov); 
  CCameraNode* ni = new CCameraNodePnP(); 
  CCameraNode* nj = new CCameraNodePnP(); 
  vo.featureExtraction(tar_cv_i_img, tar_cv_d_img, 0.001, *ni); 
  vo.featureExtraction(src_cv_i_img, src_cv_d_img, 0.001, *nj); 

  MatchingResult mr = nj->matchNodePair(ni); 

  // print_tf(std::cout, tran); 
  cout <<" mr.result: "<<endl<<mr.final_trafo<<endl; 
  
  if(!mr.succeed_match)
  {
    printf("VO failed, no result!");
    return ;
  }
  Eigen::Matrix<double , 6, 1> pv = cov_Helper(mr.final_trafo);  // cov_Helper(mr.edge.transform.matrix()); 
  nj->computeCov(ni, mr.inlier_matches, cov_Helper, cov); 

  // Eigen::Matrix<float, 4, 4> m; 
  // fromTF2Eigen(tran, m); 
  // Eigen::Matrix<double, 6, 1> pv = cov_Helper(m);

  cout <<"pose3: \n"<<pv<<endl;
  cout <<" information matrix: \n"<<cov.inverse()<<endl;

  // show point cloud 
  
 // print_tf(std::cout, tran); 

  CloudPtr pci(new Cloud); 
  CloudPtr pcj(new Cloud); 
  CloudPtr pcj_ni(new Cloud); 
  generatePointCloud(tar_cv_i_img, tar_cv_d_img, 0.001, sr4k, *pci);
  generatePointCloud(src_cv_i_img, src_cv_d_img, 0.001, sr4k, *pcj); 
  
  pcl::transformPointCloud(*pcj, *pcj_ni,  mr.final_trafo); // mr.final_trafo); 
  
  markColor(*pci, GREEN); 
  markColor(*pcj_ni, RED); 
  *pci += *pcj_ni; 
  
  CVTKViewer<pcl::PointXYZRGBA> v;
  // v.getViewer()->addCoordinateSystem(0.2, 0, 0); 
  v.addPointCloud(pci, "pci + pcj"); 
  while(!v.stopped())
  {
    v.runOnce(); 
    usleep(100*1000); 
  }

  return ;
}


void addTwoNodes()
{
  // read sr4k data from disk 
  string src_file = "../../../src/visual_odometry/data/d1_0726.bdat"; 
  string tar_file = "../../../src/visual_odometry/data/d1_0724.bdat"; 
  
  ros::NodeHandle nh("~"); 
  nh.param("src_filename", src_file, src_file); 
  nh.param("tar_filename", tar_file, tar_file); 

  // CSReader r4k; 
  CSReadCV r4k; 
  sr_data src_data; 
  sr_data tar_data; 

  // generate imgs and dpts 
  cv::Mat tar_cv_d_img(SR_HEIGHT, SR_WIDTH, CV_16UC1); 
  cv::Mat src_cv_d_img(SR_HEIGHT, SR_WIDTH, CV_16UC1); 
  cv::Mat src_cv_i_img; 
  cv::Mat tar_cv_i_img; 

  if(!r4k.readOneFrameCV(src_file, src_cv_i_img, src_cv_d_img))
  {
    ROS_INFO("%s failed to read file %s", __FILE__, src_file.c_str()); 
    return ;
  }
  if(!r4k.readOneFrameCV(tar_file, tar_cv_i_img, tar_cv_d_img))
  {
    ROS_INFO("%s failed to read file %s", __FILE__, tar_file.c_str()); 
    return ;
  }
 
  // 
  CGraphGT gt_graph; 

 // VRO 
  CamModel sr4k(250.5773, 250.5773, 90, 70, -0.8466, 0.5370); 
  CCameraNode::set_cam_cov(sr4k); 
  sr4k.z_offset = 0.015;  // this is only for sr4k 
  // CSparseFeatureVO vo(sr4k, "SIFTGPU", "SIFTGPU", "FLANN");
  CSparseFeatureVO vo(sr4k); 

  CCameraNode* ptar = new CCameraNode(); 
  CCameraNode* psrc = new CCameraNode(); 

  float depth_scale = 0.001;
  vo.featureExtraction(src_cv_i_img, src_cv_d_img, depth_scale, *psrc); 
  vo.featureExtraction(tar_cv_i_img, tar_cv_d_img, depth_scale, *ptar); 
  
  gt_graph.addNode(ptar); 
  gt_graph.addNode(psrc);
  gt_graph.optimizeGraph();

  return ;
}

void fromTF2Eigen(tf::Transform tT, Eigen::Matrix<float, 4 , 4>& tr)
{
  tf::Matrix3x3 R = tT.getBasis(); 
  tr = Eigen::Matrix<float, 4, 4>::Identity(); 

  for(int i=0; i<3; i++)
    for(int j=0; j<3; j++)
    {
      tr(i,j) = R[i][j];   
    }

  tr(0,3) = tT.getOrigin().getX(); tr(1,3) = tT.getOrigin().getY(); tr(2,3) = tT.getOrigin().getZ(); 
  return ; 

}


void map_raw_img_to_grey(unsigned short * pRaw, unsigned char* pGrey, int N)
{
  unsigned short limit_s = 65000;
  unsigned short* p1 = pRaw; 
  unsigned char* p2 = pGrey; 
  
  unsigned char max_c = 0; 
  unsigned char tc = 0; 
  static vector<float> sqrt_map_;

  if(sqrt_map_.size() <= 0)
  {
    int N = 65535; 
    sqrt_map_.resize(N);
    for(int i=0; i<N; i++)
      sqrt_map_[i] = (unsigned char)(sqrt(double(i))); 
  }
  
  for(int i=0; i<N; i++)
  {
    if(*p1 >= limit_s) // delete the values larger than 65000
      tc = 0; 
    else 
      tc = sqrt_map_[*p1];
    if(tc > max_c) {max_c = tc; }
    *p2 = tc;
    ++p1; 
    ++p2; 
  }
  assert(max_c > 0);
  p2 = pGrey;
  float inv_max = (float)(255.0/max_c);
  for(int i=0; i<N; i++)
  {
    *p2 = (unsigned char)((*p2)*inv_max);
    ++p2;
  }
}


void print_tf(ostream& out, tf::Transform tT)
{
  tfScalar r, p, y, tx, ty, tz;
  tT.getBasis().getEulerYPR(y, p, r); 
  tf::Vector3 t = tT.getOrigin(); 
  tx = t.getX(); ty = t.getY(); tz = t.getZ();
  out<<"test_vro: yaw: "<<R2D(y)<<" pitch: "<<R2D(p)<<" roll: "<<R2D(r)<<" tx: "<<tx<<" ty: "<<ty<<" tz: "<<tz<<" qx = "<<
    tT.getRotation().x()<<" qy = "<<tT.getRotation().y()<<" qz= "<<tT.getRotation().z()<<" qw = "<<tT.getRotation().w()<<endl;
}

